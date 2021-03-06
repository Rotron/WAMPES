#ifndef _PING_H
#define _PING_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _TIMER_H
#include "timer.h"
#endif

struct ping {
	struct ping *next;      /* Linked list pointers */
	struct ping *prev;
	int32 target;           /* Starting target IP address */
	int32 sent;             /* Total number of pings sent */
	int32 srtt;             /* Smoothed round trip time */
	int32 mdev;             /* Mean deviation */
	int32 responses;        /* Total number of responses */
	struct timer timer;     /* Ping interval timer */
	uint len;               /* Length of data portion of ping */
};

/* In ping.c: */
void echo_proc(int32 source,int32 dest,struct icmp *icmp,struct mbuf **bpp);
int pingem(int32 target,uint seq,uint id,uint len);

#endif /* _PING_H */

