/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/kiss.h,v 1.5 1991-04-12 18:35:05 deyke Exp $ */

#ifndef _KISS_H
#define _KISS_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* In kiss.c: */
int kiss_raw __ARGS((struct iface *iface,struct mbuf *data));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));
int32 kiss_ioctl __ARGS((struct iface *iface,int cmd,int set,int32 val));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));

#endif  /* _KISS_H */
