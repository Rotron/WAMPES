/* Generic serial line interface routines
 * Copyright 1992 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "proc.h"
#include "iface.h"
#include "netuser.h"
#include "slhc.h"
#include "n8250.h"
#include "asy.h"
#include "ax25.h"
#include "kiss.h"
#include "nrs.h"
#include "slip.h"
#include "commands.h"

static int asy_detach(struct iface *ifp);

/* Attach a serial interface to the system
 * argv[0]: hardware type, must be "asy"
 * argv[1]: I/O address, e.g., "0x3f8"
 * argv[2]: vector, e.g., "4"
 * argv[3]: mode, may be:
 *              "slip" (point-to-point SLIP)
 *              "kissui" (AX.25 UI frame format in SLIP for raw TNC)
 *              "ax25ui" (same as kissui)
 *              "kissi" (AX.25 I frame format in SLIP for raw TNC)
 *              "ax25i" (same as kissi)
 *              "nrs" (NET/ROM format serial protocol)
 *              "ppp" (Point-to-Point Protocol, RFC1171, RFC1172)
 * argv[4]: interface label, e.g., "sl0"
 * argv[5]: receiver ring buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "9600"
 * argv[8]: optional flags,
 *              'v' for Van Jacobson TCP header compression (SLIP only,
 *                  use ppp command for VJ compression with PPP);
 *              'c' for cts flow control
 *              'r' for rlsd (cd) detection
 */
int
asy_attach(
int argc,
char *argv[],
void *p)
{
	register struct iface *ifp;
	int dev;
	int trigchar = -1;
	int cts,rlsd;
	struct asymode *ap;
	int base;
	int irq;
	int chain;

	if(if_lookup(argv[4]) != NULL){
		printf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	if(setencap(NULL,argv[3]) == -1){
		printf("Unknown encapsulation %s\n",argv[3]);
		return -1;
	}
	/* Find unused asy control block */
	for(dev=0;dev < ASY_MAX;dev++){
		if(Asy[dev].iface == NULL)
			break;
	}
	if(dev >= ASY_MAX){
		printf("Too many asynch controllers\n");
		return -1;
	}

	base = htoi(argv[1]);

		irq = atoi(argv[2]);

	/* Create interface structure and fill in details */
	ifp = (struct iface *)callocw(1,sizeof(struct iface));
	ifp->addr = Ip_addr;
	ifp->name = strdup(argv[4]);
	ifp->mtu = atoi(argv[6]);
	ifp->dev = dev;
	ifp->stop = asy_detach;
	setencap(ifp,argv[3]);

	/* Look for the interface mode in the table */
	for(ap = Asymode;ap->name != NULL;ap++){
		if(stricmp(argv[3],ap->name) == 0){
			trigchar = ap->trigchar;
			if((*ap->init)(ifp) != 0){
				printf("%s: mode %s Init failed\n",
				 ifp->name,argv[3]);
				if_detach(ifp);
				return -1;
			}
			break;
		}
	}
	if(ap->name == NULL){
		printf("Mode %s unknown for interface %s\n",argv[3],argv[4]);
		if_detach(ifp);
		return -1;
	}
	/* Link in the interface */
	ifp->next = Ifaces;
	Ifaces = ifp;

	cts = rlsd = 0;
	if(argc > 8){
		if(strchr(argv[8],'c') != NULL)
			cts = 1;
		if(strchr(argv[8],'r') != NULL)
			rlsd = 1;
	}
	if(strchr(argv[2],'c') != NULL)
		chain = 1;
	else
		chain = 0;
	asy_init(dev,ifp,base,irq,(uint)atol(argv[5]),
		trigchar,(uint)atol(argv[7]),cts,rlsd,chain);
	return 0;
}

static int
asy_detach(
struct iface *ifp)
{
	struct asymode *ap;

	if(ifp == NULL)
		return -1;
	asy_stop(ifp);

	/* Call mode-dependent routine */
	for(ap = Asymode;ap->name != NULL;ap++){
		if(ifp->iftype != NULL
		 && stricmp(ifp->iftype->name,ap->name) == 0
		 && ap->free != NULL){
			(*ap->free)(ifp);
		}
	}
	return 0;
}

