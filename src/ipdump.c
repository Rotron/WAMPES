/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/ipdump.c,v 1.4 1991-02-24 20:17:02 deyke Exp $ */

/* IP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "iface.h"
#include "ip.h"
#include "trace.h"
#include "netuser.h"

void
ip_dump(fp,bpp,check)
FILE *fp;
struct mbuf **bpp;
int check;
{
	struct ip ip;
	int16 ip_len;
	int16 length;
	int16 csum;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;

	fprintf(fp,"IP:");
	/* Sneak peek at IP header and find length */
	ip_len = ((*bpp)->data[0] & 0xf) << 2;
	if(ip_len < IPLEN){
		fprintf(fp," bad header\n");
		return;
	}
	if(check)
		csum = cksum(NULLHEADER,*bpp,ip_len);
	else
		csum = 0;

	ntohip(&ip,bpp);        /* Can't fail, we've already checked ihl */

	/* Trim data segment if necessary. */
	length = ip.length - ip_len;    /* Length of data portion */
	trim_mbuf(bpp,length);
	fprintf(fp," len %u",ip.length);
	fprintf(fp," %s",inet_ntoa(ip.source));
	fprintf(fp,"->%s ihl %u ttl %u",
		inet_ntoa(ip.dest),ip_len,uchar(ip.ttl));
	if(ip.tos != 0)
		fprintf(fp," tos %u",uchar(ip.tos));
	if(ip.offset != 0 || ip.flags.mf)
		fprintf(fp," id %u offs %u",ip.id,ip.offset);
	if(ip.flags.df)
		fprintf(fp," DF");
	if(ip.flags.mf){
		fprintf(fp," MF");
		check = 0;      /* Bypass host-level checksum verify */
	}
	if(csum != 0)
		fprintf(fp," CHECKSUM ERROR (%u)",csum);

	if(ip.offset != 0){
		putc('\n',fp);
		return;
	}
	switch(uchar(ip.protocol)){
	case TCP_PTCL:
		fprintf(fp," prot TCP\n");
		tcp_dump(fp,bpp,ip.source,ip.dest,check);
		break;
	case UDP_PTCL:
		fprintf(fp," prot UDP\n");
		udp_dump(fp,bpp,ip.source,ip.dest,check);
		break;
	case ICMP_PTCL:
		fprintf(fp," prot ICMP\n");
		icmp_dump(fp,bpp,ip.source,ip.dest,check);
		break;
	default:
		fprintf(fp," prot %u\n",uchar(ip.protocol));
		break;
	}
}
