/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/crc.h,v 1.2 1992-10-16 17:57:11 deyke Exp $ */

#ifndef _CRC_H
#define _CRC_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

/* In crc.c: */
void append_crc_16 __ARGS((struct mbuf *bp));
int check_crc_16 __ARGS((struct mbuf *head));
void append_crc_rmnc __ARGS((struct mbuf *bp));
int check_crc_rmnc __ARGS((struct mbuf *head));
void append_fcs __ARGS((struct mbuf *bp));
int check_fcs __ARGS((const char *buf, int cnt));

#endif  /* _CRC_H */
