/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/ip.c,v 1.12 1994-10-09 08:22:51 deyke Exp $ */

/* Upper half of IP, consisting of send/receive primitives, including
 * fragment reassembly, for higher level protocols.
 * Not needed when running as a standalone gateway.
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "internet.h"
#include "netuser.h"
#include "iface.h"
#include "pktdrvr.h"
#include "ip.h"
#include "icmp.h"

static struct mbuf *fraghandle(struct ip *ip,struct mbuf *bp);
static void ip_timeout(void *arg);
static void free_reasm(struct reasm *rp);
static void freefrag(struct frag *fp);
static struct reasm *lookup_reasm(struct ip *ip);
static struct reasm *creat_reasm(struct ip *ip);
static struct frag *newfrag(uint16 offset,uint16 last,struct mbuf *bp);
void ttldec(struct iface *ifp);

struct mib_entry Ip_mib[20] = {
	"",                     0,
	"ipForwarding",         1,
	"ipDefaultTTL",         MAXTTL,
	"ipInReceives",         0,
	"ipInHdrErrors",        0,
	"ipInAddrErrors",       0,
	"ipForwDatagrams",      0,
	"ipInUnknownProtos",    0,
	"ipInDiscards",         0,
	"ipInDelivers",         0,
	"ipOutRequests",        0,
	"ipOutDiscards",        0,
	"ipOutNoRoutes",        0,
	"ipReasmTimeout",       TLB,
	"ipReasmReqds",         0,
	"ipReasmOKs",           0,
	"ipReasmFails",         0,
	"ipFragOKs",            0,
	"ipFragFails",          0,
	"ipFragCreates",        0,
};

struct reasm *Reasmq;
static struct raw_ip *Raw_ip;

#define INSERT  0
#define APPEND  1
#define PREPEND 2

/* Send an IP datagram. Modeled after the example interface on p 32 of
 * RFC 791
 */
int
ip_send(
int32 source,                   /* source address */
int32 dest,                     /* Destination address */
char protocol,                  /* Protocol */
char tos,                       /* Type of service */
char ttl,                       /* Time-to-live */
struct mbuf *bp,                /* Data portion of datagram */
uint16 length,                  /* Optional length of data portion */
uint16 id,                      /* Optional identification */
char df)                        /* Don't-fragment flag */
{
	struct mbuf *tbp;
	struct ip ip;                   /* IP header */
	static uint16 id_cntr = 0;      /* Datagram serial number */

	ipOutRequests++;

	if(source == INADDR_ANY)
		source = locaddr(dest);
	if(length == 0 && bp != NULLBUF)
		length = len_p(bp);
	if(id == 0)
		id = id_cntr++;
	if(ttl == 0)
		ttl = (char) ipDefaultTTL;

	/* Fill in IP header */
	ip.version = IPVERSION;
	ip.tos = tos;
	ip.length = IPLEN + length;
	ip.id = id;
	ip.offset = 0;
	ip.flags.mf = 0;
	ip.flags.df = df;
	ip.flags.congest = 0;
	ip.ttl = ttl;
	ip.protocol = protocol;
	ip.source = source;
	ip.dest = dest;
	ip.optlen = 0;
	if((tbp = htonip(&ip,bp,IP_CS_NEW)) == NULLBUF){
		free_p(bp);
		return -1;
	}
	if(ismyaddr(ip.dest)){
		/* Pretend it has been sent by the loopback interface before
		 * it appears in the receive queue
		 */
		net_route(&Loopback,tbp);
		Loopback.ipsndcnt++;
		Loopback.rawsndcnt++;
		Loopback.lastsent = secclock();
	} else
		net_route(NULLIF,tbp);
	return 0;
}

/* Reassemble incoming IP fragments and dispatch completed datagrams
 * to the proper transport module
 */
void
ip_recv(
struct iface *iface,    /* Incoming interface */
struct ip *ip,          /* Extracted IP header */
struct mbuf *bp,        /* Data portion */
int rxbroadcast)        /* True if received on subnet broadcast address */
{
	/* Function to call with completed datagram */
	register struct raw_ip *rp;
	struct mbuf *bp1,*tbp;
	int rxcnt = 0;
	register struct iplink *ipp;

	/* If we have a complete packet, call the next layer
	 * to handle the result. Note that fraghandle passes back
	 * a length field that does NOT include the IP header
	 */
	if((bp = fraghandle(ip,bp)) == NULLBUF)
		return;         /* Not done yet */

	ipInDelivers++;

	for(rp = Raw_ip;rp != NULLRIP;rp = rp->next){
		if(rp->protocol != ip->protocol)
			continue;
		rxcnt++;
		/* Duplicate the data portion, and put the header back on */
		dup_p(&bp1,bp,0,len_p(bp));
		if(bp1 != NULLBUF && (tbp = htonip(ip,bp1,IP_CS_OLD)) != NULLBUF){
			enqueue(&rp->rcvq,tbp);
			if(rp->r_upcall != NULLVFP)
				(*rp->r_upcall)(rp);
		} else {
			free_p(bp1);
		}
	}
	/* Look it up in the transport protocol table */
	for(ipp = Iplink;ipp->funct != NULL;ipp++){
		if(ipp->proto == ip->protocol)
			break;
	}
	if(ipp->funct != NULL){
		/* Found, call transport protocol */
		(*ipp->funct)(iface,ip,bp,rxbroadcast);
	} else {
		/* Not found */
		if(rxcnt == 0){
			/* Send an ICMP Protocol Unknown response... */
			ipInUnknownProtos++;
			/* ...unless it's a broadcast */
			if(!rxbroadcast){
				icmp_output(ip,bp,ICMP_DEST_UNREACH,
				 ICMP_PROT_UNREACH,NULLICMP);
			}
		}
		free_p(bp);
	}
}
/* Handle IP packets encapsulated inside IP */
void
ipip_recv(
struct iface *iface,    /* Incoming interface */
struct ip *ip,          /* Extracted IP header */
struct mbuf *bp,        /* Data portion */
int rxbroadcast)        /* True if received on subnet broadcast address */
{
	net_route(&Encap,bp);
}

/* Process IP datagram fragments
 * If datagram is complete, return it with ip->length containing the data
 * length (MINUS header); otherwise return NULLBUF
 */
static
struct mbuf *
fraghandle(
struct ip *ip,          /* IP header, host byte order */
struct mbuf *bp)        /* The fragment itself */
{
	register struct reasm *rp; /* Pointer to reassembly descriptor */
	struct frag *lastfrag,*nextfrag,*tfp;
	struct mbuf *tbp;
	uint16 i;
	uint16 last;            /* Index of first byte beyond fragment */

	last = ip->offset + ip->length - (IPLEN + ip->optlen);

	rp = lookup_reasm(ip);
	if(ip->offset == 0 && !ip->flags.mf){
		/* Complete datagram received. Discard any earlier fragments */
		if(rp != NULLREASM){
			free_reasm(rp);
			ipReasmOKs++;
		}
		return bp;
	}
	ipReasmReqds++;
	if(rp == NULLREASM){
		/* First fragment; create new reassembly descriptor */
		if((rp = creat_reasm(ip)) == NULLREASM){
			/* No space for descriptor, drop fragment */
			ipReasmFails++;
			free_p(bp);
			return NULLBUF;
		}
	}
	/* Keep restarting timer as long as we keep getting fragments */
	stop_timer(&rp->timer);
	start_timer(&rp->timer);

	/* If this is the last fragment, we now know how long the
	 * entire datagram is; record it
	 */
	if(!ip->flags.mf)
		rp->length = last;

	/* Set nextfrag to the first fragment which begins after us,
	 * and lastfrag to the last fragment which begins before us
	 */
	lastfrag = NULLFRAG;
	for(nextfrag = rp->fraglist;nextfrag != NULLFRAG;nextfrag = nextfrag->next){
		if(nextfrag->offset > ip->offset)
			break;
		lastfrag = nextfrag;
	}
	/* Check for overlap with preceeding fragment */
	if(lastfrag != NULLFRAG  && ip->offset < lastfrag->last){
		/* Strip overlap from new fragment */
		i = lastfrag->last - ip->offset;
		pullup(&bp,NULLCHAR,i);
		if(bp == NULLBUF)
			return NULLBUF; /* Nothing left */
		ip->offset += i;
	}
	/* Look for overlap with succeeding segments */
	for(; nextfrag != NULLFRAG; nextfrag = tfp){
		tfp = nextfrag->next;   /* save in case we delete fp */

		if(nextfrag->offset >= last)
			break;  /* Past our end */
		/* Trim the front of this entry; if nothing is
		 * left, remove it.
		 */
		i = last - nextfrag->offset;
		pullup(&nextfrag->buf,NULLCHAR,i);
		if(nextfrag->buf == NULLBUF){
			/* superseded; delete from list */
			if(nextfrag->prev != NULLFRAG)
				nextfrag->prev->next = nextfrag->next;
			else
				rp->fraglist = nextfrag->next;
			if(tfp->next != NULLFRAG)
				nextfrag->next->prev = nextfrag->prev;
			freefrag(nextfrag);
		} else
			nextfrag->offset = last;
	}
	/* Lastfrag now points, as before, to the fragment before us;
	 * nextfrag points at the next fragment. Check to see if we can
	 * join to either or both fragments.
	 */
	i = INSERT;
	if(lastfrag != NULLFRAG && lastfrag->last == ip->offset)
		i |= APPEND;
	if(nextfrag != NULLFRAG && nextfrag->offset == last)
		i |= PREPEND;
	switch(i){
	case INSERT:    /* Insert new desc between lastfrag and nextfrag */
		tfp = newfrag(ip->offset,last,bp);
		tfp->prev = lastfrag;
		tfp->next = nextfrag;
		if(lastfrag != NULLFRAG)
			lastfrag->next = tfp;   /* Middle of list */
		else
			rp->fraglist = tfp;     /* First on list */
		if(nextfrag != NULLFRAG)
			nextfrag->prev = tfp;
		break;
	case APPEND:    /* Append to lastfrag */
		append(&lastfrag->buf,bp);
		lastfrag->last = last;  /* Extend forward */
		break;
	case PREPEND:   /* Prepend to nextfrag */
		tbp = nextfrag->buf;
		nextfrag->buf = bp;
		append(&nextfrag->buf,tbp);
		nextfrag->offset = ip->offset;  /* Extend backward */
		break;
	case (APPEND|PREPEND):
		/* Consolidate by appending this fragment and nextfrag
		 * to lastfrag and removing the nextfrag descriptor
		 */
		append(&lastfrag->buf,bp);
		append(&lastfrag->buf,nextfrag->buf);
		nextfrag->buf = NULLBUF;
		lastfrag->last = nextfrag->last;

		/* Finally unlink and delete the now unneeded nextfrag */
		lastfrag->next = nextfrag->next;
		if(nextfrag->next != NULLFRAG)
			nextfrag->next->prev = lastfrag;
		freefrag(nextfrag);
		break;
	}
	if(rp->fraglist->offset == 0 && rp->fraglist->next == NULLFRAG
		&& rp->length != 0){
		/* We've gotten a complete datagram, so extract it from the
		 * reassembly buffer and pass it on.
		 */
		bp = rp->fraglist->buf;
		rp->fraglist->buf = NULLBUF;
		/* Tell IP the entire length */
		ip->length = rp->length + (IPLEN + ip->optlen);
		free_reasm(rp);
		ipReasmOKs++;
		return bp;
	} else
		return NULLBUF;
}
/* Arrange for receipt of raw IP datagrams */
struct raw_ip *
raw_ip(
int protocol,
void (*r_upcall)(struct raw_ip *))
{
	register struct raw_ip *rp;

	rp = (struct raw_ip *)callocw(1,sizeof(struct raw_ip));
	rp->protocol = protocol;
	rp->r_upcall = r_upcall;
	rp->next = Raw_ip;
	Raw_ip = rp;
	return rp;
}
/* Free a raw IP descriptor */
void
del_ip(
struct raw_ip *rpp)
{
	struct raw_ip *rplast = NULLRIP;
	register struct raw_ip *rp;

	/* Do sanity check on arg */
	for(rp = Raw_ip;rp != NULLRIP;rplast=rp,rp = rp->next)
		if(rp == rpp)
			break;
	if(rp == NULLRIP)
		return; /* Doesn't exist */

	/* Unlink */
	if(rplast != NULLRIP)
		rplast->next = rp->next;
	else
		Raw_ip = rp->next;
	/* Free resources */
	free_q(&rp->rcvq);
	free((char *)rp);
}

static struct reasm *
lookup_reasm(
struct ip *ip)
{
	register struct reasm *rp;
	struct reasm *rplast = NULLREASM;

	for(rp = Reasmq;rp != NULLREASM;rplast=rp,rp = rp->next){
		if(ip->id == rp->id && ip->source == rp->source
		 && ip->dest == rp->dest && ip->protocol == rp->protocol){
			if(rplast != NULLREASM){
				/* Move to top of list for speed */
				rplast->next = rp->next;
				rp->next = Reasmq;
				Reasmq = rp;
			}
			return rp;
		}

	}
	return NULLREASM;
}
/* Create a reassembly descriptor,
 * put at head of reassembly list
 */
static struct reasm *
creat_reasm(
register struct ip *ip)
{
	register struct reasm *rp;

	if((rp = (struct reasm *)calloc(1,sizeof(struct reasm))) == NULLREASM)
		return rp;      /* No space for descriptor */
	rp->source = ip->source;
	rp->dest = ip->dest;
	rp->id = ip->id;
	rp->protocol = ip->protocol;
	set_timer(&rp->timer,ipReasmTimeout * 1000L);
	rp->timer.func = ip_timeout;
	rp->timer.arg = rp;

	rp->next = Reasmq;
	Reasmq = rp;
	return rp;
}

/* Free all resources associated with a reassembly descriptor */
static void
free_reasm(
struct reasm *r)
{
	register struct reasm *rp;
	struct reasm *rplast = NULLREASM;
	register struct frag *fp;

	for(rp = Reasmq;rp != NULLREASM;rplast = rp,rp=rp->next)
		if(r == rp)
			break;
	if(rp == NULLREASM)
		return; /* Not on list */

	stop_timer(&rp->timer);
	/* Remove from list of reassembly descriptors */
	if(rplast != NULLREASM)
		rplast->next = rp->next;
	else
		Reasmq = rp->next;

	/* Free any fragments on list, starting at beginning */
	while((fp = rp->fraglist) != NULLFRAG){
		rp->fraglist = fp->next;
		free_p(fp->buf);
		free((char *)fp);
	}
	free((char *)rp);
}

/* Handle reassembly timeouts by deleting all reassembly resources */
static void
ip_timeout(
void *arg)
{
	register struct reasm *rp;

	rp = (struct reasm *)arg;
	free_reasm(rp);
	ipReasmFails++;
}
/* Create a fragment */
static struct frag *
newfrag(
uint16 offset,uint16 last,
struct mbuf *bp)
{
	struct frag *fp;

	if((fp = (struct frag *)calloc(1,sizeof(struct frag))) == NULLFRAG){
		/* Drop fragment */
		free_p(bp);
		return NULLFRAG;
	}
	fp->buf = bp;
	fp->offset = offset;
	fp->last = last;
	return fp;
}
/* Delete a fragment, return next one on queue */
static void
freefrag(
struct frag *fp)
{
	free_p(fp->buf);
	free((char *)fp);
}

