/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/n8250.h,v 1.4 1992-01-08 13:44:56 deyke Exp $ */

/* Various I/O definitions specific to asynch I/O */
#ifndef _8250_H
#define _8250_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* Asynch controller control block */
struct asy {
	struct iface *iface;

	char *ipc_socket;       /* Host:port of ipc destination */
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

#endif  /* _8250_H */
