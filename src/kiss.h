/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/kiss.h,v 1.6 1992-05-28 13:50:19 deyke Exp $ */

#ifndef _KISS_H
#define _KISS_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* In kiss.c: */
int kiss_free __ARGS((struct iface *ifp));
int kiss_raw __ARGS((struct iface *iface,struct mbuf *data));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));
int kiss_init __ARGS((struct iface *ifp,int vj));
int32 kiss_ioctl __ARGS((struct iface *iface,int cmd,int set,int32 val));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));

#endif  /* _KISS_H */
