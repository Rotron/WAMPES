/* @(#) $Id: ax25.c,v 1.30 1996-08-12 18:51:17 deyke Exp $ */

/* Low level AX.25 code:
 *  incoming frame processing (including digipeating)
 *  IP encapsulation
 *  digipeater routing
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "arp.h"
#include "slip.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "ip.h"
#include "devparam.h"
#include "lapb.h"

/* List of AX.25 multicast addresses in network format (shifted ascii).
 * Only the first entry is used for transmission, but an incoming
 * packet with any one of these destination addresses is recognized
 * as a multicast.
 */
uint8 Ax25multi[][AXALEN] = {
	'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1, /* QST */
#if 0
	'M'<<1, 'A'<<1, 'I'<<1, 'L'<<1, ' '<<1, ' '<<1, '0'<<1, /* MAIL */
#endif
	'N'<<1, 'O'<<1, 'D'<<1, 'E'<<1, 'S'<<1, ' '<<1, '0'<<1, /* NODES */
#if 0
	'I'<<1, 'D'<<1, ' '<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1, /* ID */
	'O'<<1, 'P'<<1, 'E'<<1, 'N'<<1, ' '<<1, ' '<<1, '0'<<1, /* OPEN */
	'C'<<1, 'Q'<<1, ' '<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1, /* CQ */
	'B'<<1, 'E'<<1, 'A'<<1, 'C'<<1, 'O'<<1, 'N'<<1, '0'<<1, /* BEACON */
	'R'<<1, 'M'<<1, 'N'<<1, 'C'<<1, ' '<<1, ' '<<1, '0'<<1, /* RMNC */
	'A'<<1, 'L'<<1, 'L'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1, /* ALL */
#endif
	'\0',
};
uint8 Mycall[AXALEN] = {
	'N'<<1, 'O'<<1, 'C'<<1, 'A'<<1, 'L'<<1, 'L'<<1, '0'<<1  /* NOCALL */
};
struct ax_route *Ax_routes[AXROUTESIZE];
struct iface *Axroute_default_ifp;
int Digipeat = 2;       /* Controls digipeating */

int
axi_send(
struct mbuf **bpp,
struct iface *iface,
int32 gateway,
uint8 tos
){
	return axui_send(bpp,iface,gateway,tos);
}

/* Send IP datagrams across an AX.25 link */
int
axui_send(
struct mbuf **bpp,
struct iface *iface,
int32 gateway,
uint8 tos
){
	struct mbuf *tbp;
	uint8 *hw_addr;
	struct ax25_cb *axp;

	if((hw_addr = res_arp(iface,ARP_AX25,gateway,bpp)) == NULL)
		return 0;       /* Wait for address resolution */

	/* UI frames are used for any one of the following three conditions:
	 * 1. The "low delay" bit is set in the type-of-service field.
	 * 2. The "reliability" TOS bit is NOT set and the interface is in
	 *    datagram mode.
	 * 3. The destination is the broadcast address (this is helpful
	 *    when broadcasting on an interface that's in connected mode).
	 * If Axigntos is set, TOS is ignored.
	 */
	if(Axigntos)
		tos = 0;
	if((tos & IP_COS) == DELAY
	 || ((tos & IP_COS) != RELIABILITY && (iface->send == axui_send))
	 || addreq(hw_addr,Ax25multi[0])){
		/* Use UI frame */
		return (*iface->output)(iface,hw_addr,iface->hwaddr,PID_IP,bpp);
	}
	/* Reliability is needed; use I-frames in AX.25 connection */
	if((axp = find_ax25(hw_addr)) == NULL){
		/* Open a new connection */
		struct ax25 hdr;
		memset(&hdr,0,sizeof(struct ax25));
		addrcp(hdr.dest,hw_addr);
		axp = open_ax25(&hdr,
		 AX_ACTIVE,NULL,NULL,NULL,NULL);
		if(axp == NULL){
			free_p(bpp);
			return -1;
		}
	}
	if(axp->state == LAPB_DISCONNECTED){
		est_link(axp);
		lapbstate(axp,LAPB_SETUP);
	}
	/* Insert the PID */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = PID_IP;
	if((tbp = segmenter(bpp,axp->paclen)) == NULL){
		free_p(bpp);
		return -1;
	}
	return send_ax25(axp,&tbp,-1);
}
/* Add header and send connectionless (UI) AX.25 packet.
 * Note that the calling order here must match enet_output
 * since ARP also uses it.
 */
int
ax_output(
struct iface *iface,    /* Interface to use; overrides routing table */
uint8 *dest,            /* Destination AX.25 address (7 bytes, shifted) */
uint8 *source,          /* Source AX.25 address (7 bytes, shifted) */
uint16 pid,             /* Protocol ID */
struct mbuf **bpp       /* Data field (follows PID) */
){
	/* Prepend pid to data */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = (uint8)pid;
	return axsend(iface,dest,source,LAPB_COMMAND,UI,bpp);
}
/* Common subroutine for sendframe() and ax_output() */
int
axsend(
struct iface *iface,    /* Interface to use; overrides routing table */
uint8 *dest,            /* Destination AX.25 address (7 bytes, shifted) */
uint8 *source,          /* Source AX.25 address (7 bytes, shifted) */
enum lapb_cmdrsp cmdrsp,/* Command/response indication */
int ctl,                /* Control field */
struct mbuf **bpp       /* Data field (includes PID) */
){
	struct ax25 addr;
	struct iface *ifp;
	uint8 *idest;
	int rval;

	/* If the source addr is unspecified, use the interface address */
	if(source[0] == '\0')
		source = iface->hwaddr;

	/* Do AX.25 routing */
	memset(&addr,0,sizeof(struct ax25));
	memcpy(addr.dest,dest,AXALEN);
	memcpy(addr.source,source,AXALEN);
	axroute(&addr, &ifp);
	addr.cmdrsp = cmdrsp;

	if(addr.ndigis != 0 && addr.nextdigi != addr.ndigis){
		idest = addr.digis[addr.nextdigi];
	} else {
		idest = dest;
	}

	/* Allocate mbuf for control field, and fill in */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = ctl;

	htonax25(&addr,bpp);
	/* This shouldn't be necessary because redirection has already been
	 * done at the IP router layer, but just to be safe...
	 */
	if(iface->forw != NULL){
		logsrc(iface->forw,iface->forw->hwaddr);
		logdest(iface->forw,idest);
		rval = (*iface->forw->raw)(iface->forw,bpp);
	} else {
		logsrc(iface,iface->hwaddr);
		logdest(iface,idest);
		rval = (*iface->raw)(iface,bpp);
	}
	return rval;
}
/* Process incoming AX.25 packets.
 * After optional tracing, the address field is examined. If it is
 * directed to us as a digipeater, repeat it.  If it is addressed to
 * us or to QST-0, kick it upstairs depending on the protocol ID.
 */
void
ax_recv(
struct iface *iface,
struct mbuf **bpp
){
	uint8 control;
	struct ax25 hdr;
	uint8 (*mpp)[AXALEN];
	int mcast;
	uint8 *isrc,*idest;     /* "immediate" source and destination */

	/* Pull header off packet and convert to host structure */
	if(ntohax25(&hdr,bpp) < 0){
		/* Something wrong with the header */
		iface->ax25errors++;
		free_p(bpp);
		return;
	}
	/* If there were digis in this packet and at least one has
	 * been passed, then the last passed digi is the immediate source.
	 * Otherwise it is the original source.
	 */
	if(hdr.ndigis != 0 && hdr.nextdigi != 0)
		isrc = hdr.digis[hdr.nextdigi-1];
	else
		isrc = hdr.source;

	/* If there are digis in this packet and not all have been passed,
	 * then the immediate destination is the next digi. Otherwise it
	 * is the final destination.
	 */
	if(hdr.ndigis != 0 && hdr.nextdigi != hdr.ndigis)
		idest = hdr.digis[hdr.nextdigi];
	else
		idest = hdr.dest;

	/* Don't log our own packets if we overhear them, as they're
	 * already logged by axsend() and by the digipeater code.
	 */
	if(!addreq(isrc,iface->hwaddr)){
		logsrc(iface,isrc);
		logdest(iface,idest);
	}
	/* Examine immediate destination for a multicast address */
	mcast = 0;
	for(mpp = Ax25multi;(*mpp)[0] != '\0';mpp++){
		if(addreq(idest,*mpp)){
			mcast = 1;
			break;
		}
	}
	if(!mcast && !addreq(idest,iface->hwaddr)){
		/* Not a broadcast, and not addressed to us. Inhibit
		 * transmitter to avoid colliding with addressed station's
		 * response, and discard packet.
		 */
#if 0
		if(iface->ioctl != NULL)
			(*iface->ioctl)(iface,PARAM_MUTE,1,-1);
#endif
		free_p(bpp);
		return;
	}
#if 0
	if(!mcast && iface->ioctl != NULL){
		/* Packet was sent to us; abort transmit inhibit */
		(*iface->ioctl)(iface,PARAM_MUTE,1,0);
	}
#endif
	/* At this point, packet is either addressed to us, or is
	 * a multicast.
	 */
	axroute_add(iface, &hdr, 0);
	if(hdr.nextdigi < hdr.ndigis){
		/* Packet requests digipeating. See if we can repeat it. */
		if(Digipeat && !mcast){
			/* Yes, kick it back out. htonax25 will set the
			 * repeated bit.
			 */
			hdr.nextdigi++;
			if(Digipeat == 1 ||
			   (*bpp && (*(*bpp)->data & ~PF) == UI) ||
			   addreq(hdr.source, hdr.dest)) {
				struct iface *ifp;
				axroute(&hdr, &ifp);
				htonax25(&hdr,bpp);
				if (ifp) {
					logsrc(ifp,ifp->hwaddr);
					logdest(ifp,hdr.nextdigi != hdr.ndigis ? hdr.digis[hdr.nextdigi] : hdr.dest);
					(*ifp->raw)(ifp, bpp);
				}
			} else {
				lapb_input(iface,&hdr,bpp);
			}
		}
		free_p(bpp);    /* Dispose if not forwarded */
		return;
	}
	/* If we reach this point, then the packet has passed all digis,
	 * and is either addressed to us or is a multicast.
	 */
	if(*bpp == NULL)
		return;         /* Nothing left */

	/* Sneak a peek at the control field. This kludge is necessary because
	 * AX.25 lacks a proper protocol ID field between the address and LAPB
	 * sublayers; a control value of UI indicates that LAPB is to be
	 * bypassed.
	 */
	control = *(*bpp)->data & ~PF;

	if(control == UI){
		int pid;
		struct axlink *ipp;

		(void) PULLCHAR(bpp);
		if((pid = PULLCHAR(bpp)) == -1)
			return;         /* No PID */
		/* Find network level protocol and hand it off */
		for(ipp = Axlink;ipp->funct != NULL;ipp++){
			if(ipp->pid == pid)
				break;
		}
		if(ipp->funct != NULL)
			(*ipp->funct)(iface,NULL,hdr.source,hdr.dest,bpp,mcast);
		else
			free_p(bpp);
		return;
	}
	/* Everything from here down is connected-mode LAPB, so ignore
	 * multicasts
	 */
	if(mcast){
		free_p(bpp);
		return;
	}

	lapb_input(iface,&hdr,bpp);
}

int
valid_remote_call(
const uint8 *call)
{
	uint8 (*mpp)[AXALEN];

	if (!*call || ismyax25addr(call))
		return 0;
	for (mpp = Ax25multi; (*mpp)[0]; mpp++)
		if (addreq(*mpp, call))
			return 0;
	return 1;
}

static int
axroute_hash(
const uint8 *call)
{
	int hashval;

	hashval  = ((*call++ << 23) & 0x0f000000);
	hashval |= ((*call++ << 19) & 0x00f00000);
	hashval |= ((*call++ << 15) & 0x000f0000);
	hashval |= ((*call++ << 11) & 0x0000f000);
	hashval |= ((*call++ <<  7) & 0x00000f00);
	hashval |= ((*call++ <<  3) & 0x000000f0);
	hashval |= ((*call   >>  1) & 0x0000000f);
	return hashval;
}

struct ax_route *
ax_routeptr(
const uint8 *call,
int create)
{

	struct ax_route **tp;
	struct ax_route *rp;

	tp = Ax_routes + (axroute_hash(call) % AXROUTESIZE);
	for (rp = *tp; rp && !addreq(rp->target, call); rp = rp->next)
		;
	if (!rp && create) {
		rp = (struct ax_route *) calloc(1, sizeof(struct ax_route));
		addrcp(rp->target, call);
		rp->next = *tp;
		*tp = rp;
	}
	return rp;
}

void
axroute_add(
struct iface *iface,
struct ax25 *hdr,
int perm)
{

	uint8 *call;
	uint8 *calls[MAXDIGIS+1];
	int i;
	int j;
	int ncalls = 0;
	struct ax_route *lastnode = 0;
	struct ax_route *rp;

	call = hdr->source;
	if (!valid_remote_call(call))
		return;
	calls[ncalls++] = call;
	for (i = 0; i < hdr->nextdigi; i++) {
		call = hdr->digis[i];
		if (!valid_remote_call(call))
			return;
		for (j = 0; j < ncalls; j++)
			if (addreq(call, calls[j]))
				return;
		calls[ncalls++] = call;
	}

	for (i = ncalls - 1; i >= 0; i--) {
		rp = ax_routeptr(calls[i], 1);
		if (perm || !rp->perm) {
			if (lastnode) {
				rp->digi = lastnode;
				rp->ifp = 0;
			} else {
				rp->digi = 0;
				rp->ifp = iface;
			}
			rp->perm = perm;
		}
		rp->time = secclock();
		lastnode = rp;
	}
	axroute_savefile();
}

void
axroute(
struct ax25 *hdr,
struct iface **ifpp)
{

	uint8 *idest;
	int d;
	int i;
	struct ax_route *rp;
	struct iface *ifp;

	/*** Find my last address ***/

	hdr->nextdigi = 0;
	for (i = hdr->ndigis - 1; i >= 0; i--)
		if (ismyax25addr(hdr->digis[i])) {
			hdr->nextdigi = i + 1;
			break;
		}

	/*** Remove all digipeaters before me ***/

	d = hdr->nextdigi - 1;
	if (d > 0) {
		for (i = d; i < hdr->ndigis; i++)
			addrcp(hdr->digis[i-d], hdr->digis[i]);
		hdr->ndigis -= d;
		hdr->nextdigi = 1;
	}

	/*** Add necessary digipeaters and find interface ***/

	ifp = 0;
	idest = hdr->nextdigi < hdr->ndigis ? hdr->digis[hdr->nextdigi] : hdr->dest;
	for (rp = ax_routeptr(idest, 0); rp; rp = rp->digi) {
		if (rp->digi && hdr->ndigis < MAXDIGIS) {
			for (i = hdr->ndigis - 1; i >= hdr->nextdigi; i--)
				addrcp(hdr->digis[i+1], hdr->digis[i]);
			hdr->ndigis++;
			addrcp(hdr->digis[hdr->nextdigi], rp->digi->target);
		}
		ifp = rp->ifp;
	}
	if (!ifp)
		ifp = Axroute_default_ifp;
	if (ifp && ifp->forw)
		ifp = ifp->forw;
	*ifpp = ifp;

	/*** Replace my address with hwaddr of interface ***/

	if (ifp)
		addrcp(hdr->nextdigi ? hdr->digis[0] : hdr->source, ifp->hwaddr);
}
