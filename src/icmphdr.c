/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/icmphdr.c,v 1.1 1990-09-11 13:45:33 deyke Exp $ */

#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "ip.h"
#include "icmp.h"

/* Generate ICMP header in network byte order, link data, compute checksum */
struct mbuf *
htonicmp(icmp,data)
struct icmp *icmp;
struct mbuf *data;
{
	struct mbuf *bp;
	register char *cp;
	int16 checksum;

	if((bp = pushdown(data,ICMPLEN)) == NULLBUF)
		return NULLBUF;
	cp = bp->data;

	*cp++ = icmp->type;
	*cp++ = icmp->code;
	cp = put16(cp,0);               /* Clear checksum */
	switch(icmp->type){
	case ICMP_PARAM_PROB:
		*cp++ = icmp->args.pointer;
		*cp++ = 0;
		cp = put16(cp,0);
		break;
	case ICMP_REDIRECT:
		cp = put32(cp,icmp->args.address);
		break;
	case ICMP_ECHO:
	case ICMP_ECHO_REPLY:
	case ICMP_TIMESTAMP:
	case ICMP_TIME_REPLY:
	case ICMP_INFO_RQST:
	case ICMP_INFO_REPLY:
		cp = put16(cp,icmp->args.echo.id);
		cp = put16(cp,icmp->args.echo.seq);
		break;
	default:
		cp = put32(cp,0L);
		break;
	}
	/* Compute checksum, and stash result */
	checksum = cksum(NULLHEADER,bp,len_p(bp));
	cp = &bp->data[2];
	cp = put16(cp,checksum);

	return bp;
}
/* Pull off ICMP header */
int
ntohicmp(icmp,bpp)
struct icmp *icmp;
struct mbuf **bpp;
{
	char icmpbuf[8];

	if(icmp == (struct icmp *)NULL)
		return -1;
	if(pullup(bpp,icmpbuf,8) != 8)
		return -1;
	icmp->type = icmpbuf[0];
	icmp->code = icmpbuf[1];
	switch(icmp->type){
	case ICMP_PARAM_PROB:
		icmp->args.pointer = icmpbuf[4];
		break;
	case ICMP_REDIRECT:
		icmp->args.address = get32(&icmpbuf[4]);
		break;
	case ICMP_ECHO:
	case ICMP_ECHO_REPLY:
	case ICMP_TIMESTAMP:
	case ICMP_TIME_REPLY:
	case ICMP_INFO_RQST:
	case ICMP_INFO_REPLY:
		icmp->args.echo.id = get16(&icmpbuf[4]);
		icmp->args.echo.seq = get16(&icmpbuf[6]);
		break;
	}
	return 0;
}
