/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/iface.h,v 1.18 1993-01-29 06:48:24 deyke Exp $ */

#ifndef _IFACE_H
#define _IFACE_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

#include <stdio.h>

/* Interface encapsulation mode table entry. An array of these structures
 * are initialized in config.c with all of the information necessary
 * to attach a device.
 */
struct iftype {
	char *name;             /* Name of encapsulation technique */
	int (*send) __ARGS((struct mbuf *,struct iface *,int32,int));
				/* Routine to send an IP datagram */
	int (*output) __ARGS((struct iface *,char *,char *,int,struct mbuf *));
				/* Routine to send link packet */
	char *(*format) __ARGS((char *,char *));
				/* Function that formats addresses */
	int (*scan) __ARGS((char *,char *));
				/* Reverse of format */
	int type;               /* Type field for network process */
	int hwalen;             /* Length of hardware address, if any */
	void (*rcvf) __ARGS((struct iface *,struct mbuf *));
				/* Function that handles incoming packets */
	int (*addrtest) __ARGS((struct iface *,struct mbuf *));
				/* Function that tests incoming addresses */
	void (*trace) __ARGS((FILE *,struct mbuf **,int));
				/* Function that decodes protocol headers */
	int (*dinit) __ARGS((struct iface *,int32,int,char **));
				/* Function to initialize demand dialing */
	int (*dstat) __ARGS((struct iface *));
				/* Function to display dialer status */
};
#define NULLIFT (struct iftype *)0
extern struct iftype Iftypes[];

/* Interface control structure */
struct iface {
	struct iface *next;     /* Linked list pointer */
	char *name;             /* Ascii string with interface name */

	int32 addr;             /* IP address */
	int32 broadcast;        /* Broadcast address */
	int32 netmask;          /* Network mask */

	int16 mtu;              /* Maximum transmission unit size */

	int16 trace;            /* Trace flags */
#define IF_TRACE_OUT    0x01    /* Output packets */
#define IF_TRACE_IN     0x10    /* Packets to me except broadcast */
#define IF_TRACE_ASCII  0x100   /* Dump packets in ascii */
#define IF_TRACE_HEX    0x200   /* Dump packets in hex/ascii */
#define IF_TRACE_NOBC   0x1000  /* Suppress broadcasts */
#define IF_TRACE_RAW    0x2000  /* Raw dump, if supported */
	FILE *trfp;             /* Stream to trace to */

	struct iface *forw;     /* Forwarding interface for output, if rx only */

	void (*rxproc) __ARGS((struct iface *)); /* Receiver process, if any */
	struct proc *txproc;    /* IP send process */
	struct proc *supv;      /* Supervisory process, if any */

	struct mbuf *outq;      /* IP datagram transmission queue */
	int outlim;             /* Limit on outq length */
	int txbusy;             /* Transmitter is busy */

	void *dstate;           /* Demand dialer link state, if any */
	int (*dtickle)();       /* Function to tickle dialer, if any */
	void (*dstatus)();      /* Function to display dialer state, if any */

	/* Device dependent */
	int dev;                /* Subdevice number to pass to send */
				/* To device -- control */
	int32 (*ioctl) __ARGS((struct iface *,int cmd,int set,int32 val));
				/* From device -- when status changes */
	int (*iostatus) __ARGS((struct iface *,int cmd,int32 val));
				/* Call before detaching */
	int (*stop) __ARGS((struct iface *));
	char *hwaddr;           /* Device hardware address, if any */

	/* Encapsulation dependent */
	void *edv;              /* Pointer to protocol extension block, if any */
	int xdev;               /* Associated Slip or Nrs channel, if any */
	struct iftype *iftype;  /* Pointer to appropriate iftype entry */

				/* Routine to send an IP datagram */
	int (*send) __ARGS((struct mbuf *,struct iface *,int32,int));
			/* Encapsulate any link packet */
	int (*output) __ARGS((struct iface *,char *,char *,int,struct mbuf *));
			/* Send raw packet */
	int (*raw)              __ARGS((struct iface *,struct mbuf *));
			/* Display status */
	void (*show)            __ARGS((struct iface *));

	int (*discard)          __ARGS((struct iface *,struct mbuf *));
	int (*echo)             __ARGS((struct iface *,struct mbuf *));

	/* Counters */
	int32 ipsndcnt;         /* IP datagrams sent */
	int32 rawsndcnt;        /* Raw packets sent */
	int32 iprecvcnt;        /* IP datagrams received */
	int32 rawrecvcnt;       /* Raw packets received */
	int32 lastsent;         /* Clock time of last send */
	int32 lastrecv;         /* Clock time of last receive */

	int crccontrol;         /* CRC send control */
#define CRC_OFF         0       /* Don't send CRC packets */
#define CRC_TEST_16     1       /* Send a single CRC_16 packet, then switch to CRC_TEST_RMNC */
#define CRC_TEST_RMNC   2       /* Send a single CRC_RMNC packet, then switch to CRC_OFF */
#define CRC_16          3       /* Send CRC_16 packets */
#define CRC_RMNC        4       /* Send CRC_RMNC packets */
#define CRC_CCITT       5       /* Send CRC_CCITT packets */
	int32 crcerrors;        /* Packets received with CRC errors */
	int32 ax25errors;       /* Packets received with bad ax25 header */
	int16 flags;            /* Configuration flags */
#define NO_RT_ADD       1       /* Don't call rt_add in ip_route */
};
#define NULLIF  (struct iface *)0
extern struct iface *Ifaces;    /* Head of interface list */
extern struct iface  Loopback;  /* Optional loopback interface */
extern struct iface  Encap;     /* IP-in-IP pseudo interface */

/* Header put on front of each packet sent to an interface */
struct qhdr {
	char tos;
	int32 gateway;
};
/* List of link-level receive functions, initialized in config.c */
struct rfunc {
	int class;
	void (*rcvf) __ARGS((struct iface *,struct mbuf *));
};
extern struct rfunc Rfunc[];

extern char Noipaddr[];
extern struct mbuf *Hopper;

/* In iface.c: */
int bitbucket __ARGS((struct iface *ifp,struct mbuf *bp));
int if_detach __ARGS((struct iface *ifp));
struct iface *if_lookup __ARGS((char *name));
char *if_name __ARGS((struct iface *ifp,char *comment));
void if_tx __ARGS((int dev,void *arg1,void *unused));
struct iface *ismyaddr __ARGS((int32 addr));
void network __ARGS((int i,void *v1,void *v2));
int nu_send __ARGS((struct mbuf *bp,struct iface *ifp,int32 gateway,int tos));
int nu_output __ARGS((struct iface *,char *,char *,int,struct mbuf *));
int setencap __ARGS((struct iface *ifp,char *mode));

/* In config.c: */
int net_route __ARGS((struct iface *ifp,struct mbuf *bp));

#endif  /* _IFACE_H */
