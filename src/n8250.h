/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/n8250.h,v 1.6 1992-09-01 16:52:55 deyke Exp $ */

/* Various I/O definitions specific to asynch I/O */
#ifndef _N8250_H
#define _N8250_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* Asynch controller control block */
struct asy {
	struct iface *iface;

	int fd;                 /* File descriptor */

	struct mbuf *sndq;      /* Transmit queue */

	long speed;             /* Line speed in bits per second */

	long rxints;            /* receive interrupts */
	long txints;            /* transmit interrupts */
	long rxchar;            /* Received characters */
	long txchar;            /* Transmitted characters */
	long rxhiwat;           /* High water mark on hardware rx fifo */
};

extern struct asy Asy[];

#endif  /* _N8250_H */
