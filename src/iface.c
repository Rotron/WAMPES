/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/iface.c,v 1.11 1992-05-14 13:20:06 deyke Exp $ */

/* IP interface control and configuration routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "netuser.h"
#include "ax25.h"
#include "enet.h"
#include "pktdrvr.h"
#include "cmdparse.h"
#include "commands.h"

static void showiface __ARGS((struct iface *ifp));
static int mask2width __ARGS((int32 mask));
static int ifipaddr __ARGS((int argc,char *argv[],void *p));
static int iflinkadr __ARGS((int argc,char *argv[],void *p));
static int ifbroad __ARGS((int argc,char *argv[],void *p));
static int ifnetmsk __ARGS((int argc,char *argv[],void *p));
static int ifrxbuf __ARGS((int argc,char *argv[],void *p));
static int ifmtu __ARGS((int argc,char *argv[],void *p));
static int ifforw __ARGS((int argc,char *argv[],void *p));
static int ifencap __ARGS((int argc,char *argv[],void *p));
static int ifcrc __ARGS((int argc,char *argv[],void *p));

/* Interface list header */
struct iface *Ifaces = &Loopback;

/* Loopback pseudo-interface */
struct iface Loopback = {
	&Encap,         /* Link to next entry */
	"loopback",     /* name         */
	0x7f000001L,    /* addr         127.0.0.1 */
	0xffffffffL,    /* broadcast    255.255.255.255 */
	0xffffffffL,    /* netmask      255.255.255.255 */
	MAXINT16,       /* mtu          No limit */
	0,              /* flags        */
	0,              /* trace        */
	NULLCHAR,       /* trfile       */
	NULLFILE,       /* trfp         */
	NULLIF,         /* forw         */
	NULL,           /* rxproc       */
	NULL,           /* txproc       */
	NULL,           /* supv         */
	0,              /* dev          */
	NULL,           /* (*ioctl)     */
	NULLFP,         /* (*iostatus)  */
	NULLFP,         /* (*stop)      */
	NULLCHAR,       /* hwaddr       */
	NULL,           /* extension    */
	CL_NONE,        /* type         */
	0,              /* xdev         */
	&Iftypes[0],    /* iftype       */
	NULLFP,         /* (*send)      */
	NULLFP,         /* (*output)    */
	NULLFP,         /* (*raw)       */
	NULLVFP,        /* (*status)    */
	NULLFP,         /* (*discard)   */
	NULLFP,         /* (*echo)      */
	0,              /* ipsndcnt     */
	0,              /* rawsndcnt    */
	0,              /* iprecvcnt    */
	0,              /* rawrcvcnt    */
	0,              /* lastsent     */
	0,              /* lastrecv     */
	0,              /* sendcrc      */
	0,              /* crcerrors    */
	0,              /* ax25errors   */
};
/* Encapsulation pseudo-interface */
struct iface Encap = {
	NULLIF,
	"encap",        /* name         */
	INADDR_ANY,     /* addr         0.0.0.0 */
	0xffffffffL,    /* broadcast    255.255.255.255 */
	0xffffffffL,    /* netmask      255.255.255.255 */
	MAXINT16,       /* mtu          No limit */
	0,              /* flags        */
	0,              /* trace        */
	NULLCHAR,       /* trfile       */
	NULLFILE,       /* trfp         */
	NULLIF,         /* forw         */
	NULL,           /* rxproc       */
	NULL,           /* txproc       */
	NULL,           /* supv         */
	0,              /* dev          */
	NULL,           /* (*ioctl)     */
	NULLFP,         /* (*iostatus)  */
	NULLFP,         /* (*stop)      */
	NULLCHAR,       /* hwaddr       */
	NULL,           /* extension    */
	CL_NONE,        /* type         */
	0,              /* xdev         */
	&Iftypes[0],    /* iftype       */
	ip_encap,       /* (*send)      */
	NULLFP,         /* (*output)    */
	NULLFP,         /* (*raw)       */
	NULLVFP,        /* (*status)    */
	NULLFP,         /* (*discard)   */
	NULLFP,         /* (*echo)      */
	0,              /* ipsndcnt     */
	0,              /* rawsndcnt    */
	0,              /* iprecvcnt    */
	0,              /* rawrcvcnt    */
	0,              /* lastsent     */
	0,              /* lastrecv     */
	0,              /* sendcrc      */
	0,              /* crcerrors    */
	0,              /* ax25errors   */
};

char Noipaddr[] = "IP address field missing, and ip address not set\n";

struct cmds Ifcmds[] = {
	"broadcast",            ifbroad,        0,      2,      NULLCHAR,
	"crc",                  ifcrc,          0,      2,      NULLCHAR,
	"encapsulation",        ifencap,        0,      2,      NULLCHAR,
	"forward",              ifforw,         0,      2,      NULLCHAR,
	"ipaddress",            ifipaddr,       0,      2,      NULLCHAR,
	"linkaddress",          iflinkadr,      0,      2,      NULLCHAR,
	"mtu",                  ifmtu,          0,      2,      NULLCHAR,
	"netmask",              ifnetmsk,       0,      2,      NULLCHAR,
	"rxbuf",                ifrxbuf,        0,      2,      NULLCHAR,
	NULLCHAR,
};

/* Process packets in the Hopper */
void
network(i,v1,v2)
int i;
void *v1;
void *v2;
{
	struct mbuf *bp;
	struct phdr phdr;

loop:
	while(Hopper == NULLBUF)
		pwait(&Hopper);

	/* Process the input packet */
	bp = dequeue(&Hopper);
	pullup(&bp,(char *)&phdr,sizeof(phdr));
	if(phdr.iface != NULLIF){
		phdr.iface->rawrecvcnt++;
		phdr.iface->lastrecv = secclock();
	}
	dump(phdr.iface,IF_TRACE_IN,phdr.type,bp);
	switch(phdr.type){
#ifdef  KISS
	case CL_KISS:
		kiss_recv(phdr.iface,bp);
		break;
#endif
#ifdef  AX25
	case CL_AX25:
		ax_recv(phdr.iface,bp);
		break;
#endif
#ifdef  ETHER
	case CL_ETHERNET:
		eproc(phdr.iface,bp);
		break;
#endif
#ifdef ARCNET
	case CL_ARCNET:
		aproc(phdr.iface,bp);
		break;
#endif
#ifdef PPP
	case CL_PPP:
		ppp_proc(phdr.iface,bp);
		break;
#endif
	/* These types have no link layer protocol at the point when they're
	 * put in the hopper, so they can be handed directly to IP. The
	 * separate types are just for user convenience when running the
	 * "iface" command.
	 */
	case CL_NONE:
	case CL_SERIAL_LINE:
	case CL_SLFP:
		ip_route(phdr.iface,bp,0);
		break;
	default:
		free_p(bp);
		break;
	}
	/* Let everything else run - this keeps the system from wedging
	 * when we're hit by a big burst of packets
	 */
	pwait(NULL);
	goto loop;
}

/* put mbuf into Hopper for network task
 * returns 0 if OK
 */
int
net_route(ifp, type, bp)
struct iface *ifp;
int type;
struct mbuf *bp;
{
	struct mbuf *nbp;
	struct phdr phdr;

	phdr.iface = ifp;
	phdr.type = type;

	if ((nbp = pushdown(bp,sizeof(phdr))) == NULLBUF ){
		return -1;
	}
	memcpy( &nbp->data[0],(char *)&phdr,sizeof(phdr));
	enqueue(&Hopper,nbp);
	/* Especially on slow machines, serial I/O can be quite
	 * compute intensive, so release the machine before we
	 * do the next packet.  This will allow this packet to
	 * go on toward its ultimate destination. [Karn]
	 */
	pwait(NULL);
	return 0;
}

/* Set interface parameters */
int
doifconfig(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next)
			showiface(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULLIF){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc == 2){
		showiface(ifp);
		if ( ifp->show != NULLVFP ) {
			(*ifp->show)(ifp);
		}
		return 0;
	}
	if(argc == 3){
		printf("Argument missing\n");
		return 1;
	}
	for(i=2;i<argc-1;i+=2)
		subcmd(Ifcmds,3,&argv[i-1],ifp);

	return 0;
}

/* Set interface IP address */
static int
ifipaddr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	ifp->addr = resolve(argv[1]);
	return 0;
}

/* Set link (hardware) address */
static int
iflinkadr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	if(ifp->iftype == NULLIFT || ifp->iftype->scan == NULL){
		printf("Can't set link address\n");
		return 1;
	}
	if(ifp->hwaddr != NULLCHAR)
		free(ifp->hwaddr);
	ifp->hwaddr = mallocw(ifp->iftype->hwalen);
	(*ifp->iftype->scan)(ifp->hwaddr,argv[1]);
	return 0;
}

/* Set interface broadcast address. This is actually done
 * by installing a private entry in the routing table.
 */
static int
ifbroad(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;
	struct route *rp;

	rp = rt_blookup(ifp->broadcast,32);
	if(rp != NULLROUTE && rp->iface == ifp)
		rt_drop(ifp->broadcast,32);
	ifp->broadcast = resolve(argv[1]);
	rt_add(ifp->broadcast,32,0L,ifp,1L,0L,1);
	return 0;
}

/* Set the network mask. This is actually done by installing
 * a routing entry.
 */
static int
ifnetmsk(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;
	struct route *rp;

	/* Remove old entry if it exists */
	rp = rt_blookup(ifp->addr & ifp->netmask,mask2width(ifp->netmask));
	if(rp != NULLROUTE)
		rt_drop(rp->target,rp->bits);

	ifp->netmask = htol(argv[1]);
	rt_add(ifp->addr,mask2width(ifp->netmask),0L,ifp,0L,0L,0);
	return 0;
}

/* Command to set interface encapsulation mode */
static int
ifencap(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	if(setencap(ifp,argv[1]) != 0){
		printf("Encapsulation mode '%s' unknown\n",argv[1]);
		return 1;
	}
	return 0;
}
/* Function to set encapsulation mode */
int
setencap(ifp,mode)
struct iface *ifp;
char *mode;
{
	struct iftype *ift;

	for(ift = &Iftypes[0];ift->name != NULLCHAR;ift++)
		if(strnicmp(ift->name,mode,strlen(mode)) == 0)
			break;
	if(ift->name == NULLCHAR){
		return -1;
	}
	ifp->iftype = ift;
	ifp->send = ift->send;
	ifp->output = ift->output;
	ifp->type = ift->type;
	return 0;
}
/* Set interface receive buffer size */
static int
ifrxbuf(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return 0;       /* To be written */
}

/* Set interface Maximum Transmission Unit */
static int
ifmtu(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	ifp->mtu = atoi(argv[1]);
	return 0;
}

/* Set interface forwarding */
static int
ifforw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	ifp->forw = if_lookup(argv[1]);
	if(ifp->forw == ifp)
		ifp->forw = NULLIF;
	return 0;
}

static int
ifcrc(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	return setbool(&ifp->sendcrc, "CRC generation", argc, argv);
}

/* Display the parameters for a specified interface */
static void
showiface(ifp)
register struct iface *ifp;
{
	char tmp[25];

	printf("%-10s IP addr %s MTU %u Link encap ",ifp->name,
	 inet_ntoa(ifp->addr),(int)ifp->mtu);
	if(ifp->iftype == NULLIFT){
		printf("not set\n");
	} else {
		printf("%s\n",ifp->iftype->name);
		if(ifp->iftype->format != NULL && ifp->hwaddr != NULLCHAR)
			printf("           Link addr %s\n",
			 (*ifp->iftype->format)(tmp,ifp->hwaddr));
	}
	printf("           flags %u trace 0x%x netmask 0x%08lx broadcast %s\n",
		ifp->flags,ifp->trace,ifp->netmask,inet_ntoa(ifp->broadcast));
	if(ifp->forw != NULLIF)
		printf("           output forward to %s\n",ifp->forw->name);
	printf("           sent: ip %lu tot %lu idle %s\n",
	 ifp->ipsndcnt,ifp->rawsndcnt,tformat(secclock() - ifp->lastsent));
	printf("           recv: ip %lu tot %lu idle %s\n",
	 ifp->iprecvcnt,ifp->rawrecvcnt,tformat(secclock() - ifp->lastrecv));
	printf("           CRC %s errors %lu\n",
	 ifp->sendcrc ? "enabled" : "disabled", ifp->crcerrors);
	printf("           bad ax25 headers %lu\n",
	 ifp->ax25errors);
	printf("\n");
}

/* Given the ascii name of an interface, return a pointer to the structure,
 * or NULLIF if it doesn't exist
 */
struct iface *
if_lookup(name)
char *name;
{
	register struct iface *ifp;

	for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
		if(strcmp(ifp->name,name) == 0)
			break;
	return ifp;
}

/* Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULLIF otherwise.
 * This is used to tell if an incoming IP datagram is for us, or if it
 * has to be routed.
 */
struct iface *
ismyaddr(addr)
int32 addr;
{
	register struct iface *ifp;

	for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
		if(addr == ifp->addr)
			break;
	return ifp;
}

/* Given a network mask, return the number of contiguous 1-bits starting
 * from the most significant bit.
 */
static int
mask2width(mask)
int32 mask;
{
	int width,i;

	width = 0;
	for(i = 31;i >= 0;i--){
		if(!(mask & (1L << i)))
			break;
		width++;
	}
	return width;
}

/* return buffer with name + comment */
char *
if_name(ifp,comment)
struct iface *ifp;
char *comment;
{
	char *result = mallocw( strlen(ifp->name) + strlen(comment) + 1 );
	strcpy( result, ifp->name );
	return strcat( result, comment );
}

/* Raw output routine that tosses all packets. Used by dialer, tip, etc */
int
bitbucket(ifp,bp)
struct iface *ifp;
struct mbuf *bp;
{
	free_p(bp);
	return 0;
}
