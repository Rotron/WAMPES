/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/arphdr.c,v 1.8 1996-08-11 18:16:09 deyke Exp $ */

/* ARP header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "arp.h"

/* Copy a host format arp structure into mbuf for transmission */
struct mbuf *
htonarp(
register struct arp *arp)
{
	struct mbuf *bp;
	register uint8 *buf;

	if(arp == (struct arp *)NULL)
		return NULL;

	bp = ambufw(ARPLEN + 2 * arp->hwalen);
	buf = bp->data;

	buf = put16(buf,arp->hardware);
	buf = put16(buf,arp->protocol);
	*buf++ = arp->hwalen;
	*buf++ = arp->pralen;
	buf = put16(buf,arp->opcode);
	memcpy(buf,arp->shwaddr,(uint16)arp->hwalen);
	buf += arp->hwalen;
	buf = put32(buf,arp->sprotaddr);
	memcpy(buf,arp->thwaddr,(uint16)arp->hwalen);
	buf += arp->hwalen;
	buf = put32(buf,arp->tprotaddr);

	bp->cnt = buf - bp->data;
	return bp;
}
/* Convert an incoming ARP packet into a host-format structure */
int
ntoharp(
struct arp *arp,
struct mbuf **bpp
){
	if(arp == (struct arp *)NULL || bpp == NULL)
		return -1;

	arp->hardware = (enum arp_hwtype) pull16(bpp);
	arp->protocol = (uint16) pull16(bpp);
	arp->hwalen = PULLCHAR(bpp);
	arp->pralen = PULLCHAR(bpp);
	arp->opcode = (enum arp_opcode) pull16(bpp);
	pullup(bpp,arp->shwaddr,(uint16)arp->hwalen);
	arp->sprotaddr = pull32(bpp);
	pullup(bpp,arp->thwaddr,(uint16)arp->hwalen);
	arp->tprotaddr = pull32(bpp);

	/* Get rid of anything left over */
	free_p(bpp);
	return 0;
}

