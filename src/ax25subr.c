/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/ax25subr.c,v 1.3 1990-08-23 17:32:32 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include <ctype.h>

/*
 * setcall - convert callsign plus substation ID of the form
 * "KA9Q-0" to AX.25 (shifted) address format
 *   Address extension bit is left clear
 *   Return -1 on error, 0 if OK
 */
int
setcall(out,call)
struct ax25_addr *out;
char *call;
{
	int csize;
	unsigned ssid;
	register int i;
	register char *cp,*dp;
	char c;

	if(out == (struct ax25_addr *)NULL || call == NULLCHAR || *call == '\0'){
		return -1;
	}
	/* Find dash, if any, separating callsign from ssid
	 * Then compute length of callsign field and make sure
	 * it isn't excessive
	 */
	dp = strchr(call,'-');
	if(dp == NULLCHAR)
		csize = strlen(call);
	else
		csize = dp - call;
	if(csize > ALEN)
		return -1;
	/* Now find and convert ssid, if any */
	if(dp != NULLCHAR){
		dp++;   /* skip dash */
		ssid = atoi(dp);
		if(ssid > 15)
			return -1;
	} else
		ssid = 0;
	/* Copy upper-case callsign, left shifted one bit */
	cp = out->call;
	for(i=0;i<csize;i++){
		c = *call++;
		if(islower(c))
			c = toupper(c);
		*cp++ = c << 1;
	}
	/* Pad with shifted spaces if necessary */
	for(;i<ALEN;i++)
		*cp++ = ' ' << 1;

	/* Insert substation ID field and set reserved bits */
	out->ssid = 0x60 | (ssid << 1);
	return 0;
}
/* Set a digipeater string in an ARP table entry */
setpath(out,in,cnt)
char *out;      /* Target char array containing addresses in net form */
char *in[];     /* Input array of tokenized callsigns in ASCII */
int cnt;        /* Number of callsigns in array */
{
	struct ax25_addr addr;
	char *putaxaddr();

	if(cnt == 0)
		return;
	while(cnt-- != 0){
		setcall(&addr,*in++);
		addr.ssid &= ~E;
		out = putaxaddr(out,&addr);
	}
	out[-1] |= E;
}

int
addreq(pa, pb)
struct ax25_addr *pa, *pb;
{

  register char  *a = (char *) pa;
  register char  *b = (char *) pb;

  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  return (*a & SSID) == (*b & SSID);
}

void
addrcp(to, from)
struct ax25_addr *to, *from;
{

  register char  *t = (char *) to;
  register char  *f = (char *) from;

  *t++ = *f++;
  *t++ = *f++;
  *t++ = *f++;
  *t++ = *f++;
  *t++ = *f++;
  *t++ = *f++;
  *t = (*f & SSID) | 0x60;
}

/* Convert encoded AX.25 address to printable string */
pax25(e,addr)
char *e;
struct ax25_addr *addr;
{
	register int i;
	char c,*cp;

	cp = addr->call;
	for(i=ALEN;i != 0;i--){
		c = (*cp++ >> 1) & 0x7f;
		if(c == ' ')
			break;
		*e++ = c;
	}
	if ((addr->ssid & SSID) != 0)
		sprintf(e,"-%d",(addr->ssid >> 1) & 0xf);       /* ssid */
	else
		*e = 0;
}
/* Print a string of AX.25 addresses in the form
 * "KA9Q-0 [via N4HY-0,N2DSY-2]"
 * Designed for use by ARP - arg is a char string
 */
char *
psax25(e,addr)
char *e;
char *addr;
{
	int i;
	struct ax25_addr axaddr;
	char tmp[16];
	char *getaxaddr();
	char *cp;

	cp = e;
	*cp = '\0';
	for(i=0;;i++){
		/* Create local copy in host-format structure */
		addr = getaxaddr(&axaddr,addr);

		/* Create ASCII representation and append to output */
		pax25(tmp,&axaddr);
		strcat(cp,tmp);

		if(axaddr.ssid & E)
			break;
		if(i == 0)
			strcat(cp," via ");
		else
			strcat(cp,",");
		/* Not really necessary, but speeds up subsequent strcats */
		cp += strlen(cp);
	}
	return e;
}
char *
getaxaddr(ap,cp)
register struct ax25_addr *ap;
register char *cp;
{
	memcpy(ap->call,cp,ALEN);
	cp += ALEN;
	ap->ssid = *cp++;
	return cp;
}
char *
putaxaddr(cp,ap)
register char *cp;
register struct ax25_addr *ap;
{
	memcpy(cp,ap->call,ALEN);
	cp += ALEN;
	*cp++ = ap->ssid;
	return cp;
}

/* Convert a host-format AX.25 header into a mbuf ready for transmission */
struct mbuf *
htonax25(hdr,data)
register struct ax25 *hdr;
struct mbuf *data;
{
	struct mbuf *bp;
	register char *cp;
	register int16 i;

	if(hdr == (struct ax25 *)NULL || hdr->ndigis > MAXDIGIS)
		return NULLBUF;

	/* Allocate space for return buffer */
	i = AXALEN * (2 + hdr->ndigis);
	if((bp = pushdown(data,i)) == NULLBUF)
		return NULLBUF;

	/* Now convert */
	cp = bp->data;

	hdr->dest.ssid &= ~E;   /* Dest E-bit is always off */
	/* Encode command/response in C bits */
	switch(hdr->cmdrsp){
	case LAPB_COMMAND:
		hdr->dest.ssid |= C;
		hdr->source.ssid &= ~C;
		break;
	case LAPB_RESPONSE:
		hdr->dest.ssid &= ~C;
		hdr->source.ssid |= C;
		break;
	default:
		hdr->dest.ssid &= ~C;
		hdr->source.ssid &= ~C;
		break;
	}
	cp = putaxaddr(cp,&hdr->dest);

	/* Set E bit on source address if no digis */
	if(hdr->ndigis == 0){
		hdr->source.ssid |= E;
		putaxaddr(cp,&hdr->source);
		return bp;
	}
	hdr->source.ssid &= ~E;
	cp = putaxaddr(cp,&hdr->source);

	/* All but last digi get copied with E bit off */
	for(i=0; i < hdr->ndigis - 1; i++){
		hdr->digis[i].ssid &= ~E;
		cp = putaxaddr(cp,&hdr->digis[i]);
	}
	hdr->digis[i].ssid |= E;
	cp = putaxaddr(cp,&hdr->digis[i]);
	return bp;
}
/* Convert an AX.25 ARP table entry into a host format address structure
 * ready for use in transmitting a packet
 */
int
atohax25(hdr,hwaddr,source)
register struct ax25 *hdr;
register char *hwaddr;
struct ax25_addr *source;
{
	extern struct ax25_addr mycall;
	register struct ax25_addr *axp;

	hwaddr = getaxaddr(&hdr->dest,hwaddr);  /* Destination address */
	ASSIGN(hdr->source,*source);            /* Source address */
	if(hdr->dest.ssid & E){
		/* No digipeaters */
		hdr->ndigis = 0;
		hdr->dest.ssid &= ~E;
		hdr->source.ssid |= E;
		return 2;
	}
	hdr->source.ssid &= ~E;
	hdr->dest.ssid &= ~E;
	for(axp = hdr->digis; axp < &hdr->digis[MAXDIGIS]; axp++){
		hwaddr = getaxaddr(axp,hwaddr);
		if(axp->ssid & E){
			hdr->ndigis = axp - hdr->digis + 1;
			return hdr->ndigis;
		}
	}
	return -1;
}
/* Convert a network-format AX.25 header into a host format structure
 * Return -1 if error, number of addresses if OK
 */
int
ntohax25(hdr,bpp)
register struct ax25 *hdr;      /* Output structure */
struct mbuf **bpp;
{
	register struct ax25_addr *axp;
	char *getaxaddr();
	char buf[AXALEN];

	if(pullup(bpp,buf,AXALEN) < AXALEN)
		return -1;
	getaxaddr(&hdr->dest,buf);

	if(pullup(bpp,buf,AXALEN) < AXALEN)
		return -1;
	getaxaddr(&hdr->source,buf);

	/* Process C bits to get command/response indication */
	if((hdr->source.ssid & C) == (hdr->dest.ssid & C))
		hdr->cmdrsp = LAPB_UNKNOWN;
	else if(hdr->source.ssid & C)
		hdr->cmdrsp = LAPB_RESPONSE;
	else
		hdr->cmdrsp = LAPB_COMMAND;

	hdr->ndigis = 0;
	if(hdr->source.ssid & E)
		return 2;       /* No digis */

	/* Process digipeaters */
	for(axp = hdr->digis;axp < &hdr->digis[MAXDIGIS]; axp++){
		if(pullup(bpp,buf,AXALEN) < AXALEN)
			return -1;
		getaxaddr(axp,buf);
		if(axp->ssid & E){      /* Last one */
			hdr->ndigis = axp - hdr->digis + 1;
			return hdr->ndigis + 2;
		}
	}
	return -1;      /* Too many digis */
}

/* Figure out the frame type from the control field
 * This is done by masking out any sequence numbers and the
 * poll/final bit after determining the general class (I/S/U) of the frame
 */
int16
ftype(control)
register char control;
{
	if((control & 1) == 0)  /* An I-frame is an I-frame... */
		return I;
	if(control & 2)         /* U-frames use all except P/F bit for type */
		return(control & ~PF);
	else                    /* S-frames use low order 4 bits for type */
		return(control & 0xf);
}

