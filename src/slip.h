/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/slip.h,v 1.11 1993-01-29 06:48:38 deyke Exp $ */

#ifndef _SLIP_H
#define _SLIP_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _SLHC_H
#include "slhc.h"
#endif

#define SLIP_MAX 16             /* Maximum number of slip channels */

/* SLIP definitions */
#define SLIP_ALLOC      512     /* Receiver allocation increment */

#define FR_END          0300    /* Frame End */
#define FR_ESC          0333    /* Frame Escape */
#define T_FR_END        0334    /* Transposed frame end */
#define T_FR_ESC        0335    /* Transposed frame escape */

/* Slip protocol control structure */
struct slip {
	struct iface *iface;
	char escaped;           /* Receiver State control flag */
#define SLIP_FLAG       0x01            /* Last char was a frame escape */
#define SLIP_VJCOMPR    0x02            /* TCP header compression enabled */
	struct mbuf *rbp_head;  /* Head of mbuf chain being filled */
	struct mbuf *rbp_tail;  /* Pointer to mbuf currently being written */
	char *rcp;              /* Write pointer */
	int16 rcnt;             /* Length of mbuf chain */
	struct mbuf *tbp;       /* Transmit mbuf being sent */
	int16 errors;           /* Receiver input errors */
	int type;               /* Protocol of input */
	int (*send) __ARGS((int,struct mbuf *));        /* send mbufs to device */
	int (*get) __ARGS((int,char *,int));            /* fetch input chars from device */
	struct slcompress *slcomp;      /* TCP header compression table */
};

/* In slip.c: */
extern struct slip Slip[];

void asytxdone __ARGS((int dev));
int slip_free __ARGS((struct iface *ifp));
int slip_init __ARGS((struct iface *ifp));
int slip_raw __ARGS((struct iface *iface,struct mbuf *data));
void slip_rx __ARGS((struct iface *iface));
int slip_send __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,int tos));
int vjslip_send __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,int tos));
void slip_status __ARGS((struct iface *iface));

#endif  /* _SLIP_H */
