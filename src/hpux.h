/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/hpux.h,v 1.6 1990-10-22 11:37:56 deyke Exp $ */

#ifndef HPUX_INCLUDED
#define HPUX_INCLUDED

#include "global.h"

extern long  currtime;

extern int  chkread[];
extern int  actread[];
extern void (*readfnc[])();
extern char  *readarg[];

extern int  chkwrite[];
extern int  actwrite[];
extern void (*writefnc[])();
extern char  *writearg[];

extern int  chkexcp[];
extern int  actexcp[];
extern void (*excpfnc[])();
extern char  *excparg[];

#define setmask(mask, fd) ((mask)[(fd)>>5] |=  (1 << ((fd) & 31)))
#define clrmask(mask, fd) ((mask)[(fd)>>5] &= ~(1 << ((fd) & 31)))
#define maskset(mask, fd) ((mask)[(fd)>>5] &   (1 << ((fd) & 31)))

struct iface;   /* announce struct iface */
struct mbuf;    /* announce struct mbuf */

/* In hpux.c: */
void ioinit __ARGS((void));
void iostop __ARGS((void));
int kbread __ARGS((void));
int asy_stop __ARGS((struct iface *iface));
int asy_init __ARGS((int dev, struct iface *iface, char *arg1, char *arg2, unsigned bufsize, int trigchar, int cts));
int asy_speed __ARGS((int dev, long speed));
int asy_ioctl __ARGS((struct iface *iface, int argc, char *argv []));
int get_asy __ARGS((int dev, char *buf, int cnt));
int asy_send __ARGS((int dev, struct mbuf *bp));
void check_time __ARGS((void));
int system __ARGS((const char *cmdline));
int _system __ARGS((char *cmdline));
int eihalt __ARGS((void));

#endif  /* HPUX_INCLUDED */

