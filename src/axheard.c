/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/axheard.c,v 1.2 1994-10-06 16:15:21 deyke Exp $ */

/* AX25 link callsign monitoring. Also contains beginnings of
 * an automatic link quality monitoring scheme (incomplete)
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "ip.h"
#include "timer.h"

static struct lq *al_create(struct iface *ifp,char *addr);
static struct ld *ad_lookup(struct iface *ifp,char *addr,int sort);
static struct ld *ad_create(struct iface *ifp,char *addr);
struct lq *Lq;
struct ld *Ld;

#ifdef  notdef
/* Send link quality reports to interface */
void
genrpt(
struct iface *ifp)
{
	struct mbuf *bp;
	register char *cp;
	int i;
	struct lq *lp;
	int maxentries,nentries;

	maxentries = (Paclen - LQHDR) / LQENTRY;
	if((bp = alloc_mbuf(Paclen)) == NULLBUF)
		return;
	cp = bp->data;
	nentries = 0;

	/* Build and emit header */
	cp = putlqhdr(cp,LINKVERS,Ip_addr);

	/* First entry is for ourselves. Since we're examining the Axsent
	 * variable before we've sent this frame, add one to it so it'll
	 * match the receiver's count after he gets this frame.
	 */
	cp = putlqentry(cp,ifp->hwaddr,Axsent+1);
	nentries++;

	/* Now add entries from table */
	for(lp = lq;lp != NULLLQ;lp = lp->next){
		cp = putlqentry(cp,&lp->addr,lp->currxcnt);
		if(++nentries >= MAXENTRIES){
			/* Flush */
			bp->cnt = nentries*LQENTRY + LQHDR;
			ax_output(ifp,Ax25multi[0],ifp->hwaddr,PID_LQ,bp);
			if((bp = alloc_mbuf(Paclen)) == NULLBUF)
				return;
			cp = bp->data;
		}
	}
	if(nentries > 0){
		bp->cnt = nentries*LQENTRY + LQHDR;
		ax_output(ifp,Ax25multi[0],ifp->hwaddr,LQPID,bp);
	} else {
		free_p(bp);
	}
}

/* Pull the header off a link quality packet */
void
getlqhdr(
struct lqhdr *hp,
struct mbuf **bpp)
{
	hp->version = pull16(bpp);
	hp->ip_addr = pull32(bpp);
}

/* Put a header on a link quality packet.
 * Return pointer to buffer immediately following header
 */
char *
putlqhdr(
register char *cp,
uint16 version,
int32 ip_addr)
{
	cp = put16(cp,version);
	return put32(cp,ip_addr);
}

/* Pull an entry off a link quality packet */
void
getlqentry(
struct lqentry *ep,
struct mbuf **bpp)
{
	pullup(bpp,ep->addr,AXALEN);
	ep->count = pull32(bpp);
}

/* Put an entry on a link quality packet
 * Return pointer to buffer immediately following header
 */
char *
putlqentry(
char *cp,
char *addr,
int32 count)
{
	memcpy(cp,addr,AXALEN);
	cp += AXALEN;
	return put32(cp,count);
}
#endif

/* Log the source address of an incoming packet */
void
logsrc(
struct iface *ifp,
char *addr)
{
	register struct lq *lp;

	if((lp = al_lookup(ifp,addr,1)) == NULLLQ
	 && (lp = al_create(ifp,addr)) == NULLLQ)
		return;
	lp->currxcnt++;
	lp->time = secclock();
}
/* Log the destination address of an incoming packet */
void
logdest(
struct iface *ifp,
char *addr)
{
	register struct ld *lp;

	if((lp = ad_lookup(ifp,addr,1)) == NULLLD
	 && (lp = ad_create(ifp,addr)) == NULLLD)
		return;
	lp->currxcnt++;
	lp->time = secclock();
}
/* Look up an entry in the source data base */
struct lq *
al_lookup(
struct iface *ifp,
char *addr,
int sort)
{
	register struct lq *lp;
	struct lq *lplast = NULLLQ;

	for(lp = Lq;lp != NULLLQ;lplast = lp,lp = lp->next){
		if(addreq(lp->addr,addr) && lp->iface == ifp){
			if(sort && lplast != NULLLQ){
				/* Move entry to top of list */
				lplast->next = lp->next;
				lp->next = Lq;
				Lq = lp;
			}
			return lp;
		}
	}
	return NULLLQ;
}
/* Create a new entry in the source database */
static struct lq *
al_create(
struct iface *ifp,
char *addr)
{
	register struct lq *lp;

	lp = (struct lq *)callocw(1,sizeof(struct lq));
	memcpy(lp->addr,addr,AXALEN);
	lp->next = Lq;
	Lq = lp;
	lp->iface = ifp;

	return lp;
}
/* Look up an entry in the destination database */
static struct ld *
ad_lookup(
struct iface *ifp,
char *addr,
int sort)
{
	register struct ld *lp;
	struct ld *lplast = NULLLD;

	for(lp = Ld;lp != NULLLD;lplast = lp,lp = lp->next){
		if(lp->iface == ifp && addreq(lp->addr,addr)){
			if(sort && lplast != NULLLD){
				/* Move entry to top of list */
				lplast->next = lp->next;
				lp->next = Ld;
				Ld = lp;
			}
			return lp;
		}
	}
	return NULLLD;
}
/* Create a new entry in the destination database */
static struct ld *
ad_create(
struct iface *ifp,
char *addr)
{
	register struct ld *lp;

	lp = (struct ld *)callocw(1,sizeof(struct ld));
	memcpy(lp->addr,addr,AXALEN);
	lp->next = Ld;
	Ld = lp;
	lp->iface = ifp;

	return lp;
}

