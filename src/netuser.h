/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/netuser.h,v 1.3 1990-09-11 13:46:10 deyke Exp $ */

/* Global structures and constants needed by an Internet user process */
#ifndef NCONN

#include "global.h"

#define NCONN   20              /* Maximum number of open network connections */

extern int32 Ip_addr;   /* Our IP address */
extern int Net_error;   /* Error return code */
#define NONE    0               /* No error */
#define CON_EXISTS      1       /* Connection already exists */
#define NO_CONN 2               /* Connection does not exist */
#define CON_CLOS        3       /* Connection closing */
#define NO_MEM          4       /* No memory for TCB creation */
#define WOULDBLK        5       /* Would block */
#define NOPROTO         6       /* Protocol or mode not supported */
#define INVALID         7       /* Invalid arguments */

#define INET_EOL        "\r\n"  /* Standard Internet end-of-line sequence */

/* Codes for the tcp_open call */
#define TCP_PASSIVE     0
#define TCP_ACTIVE      1
#define TCP_SERVER      2       /* Passive, clone on opening */

/* Local IP wildcard address */
#define INADDR_ANY      0x0L

/* Socket structure */
struct socket {
	int32 address;          /* IP address */
	int16 port;             /* port number */
};
#define NULLSOCK        (struct socket *)0

/* Connection structure (two sockets) */
struct connection {
	struct socket local;
	struct socket remote;
};
/* In domain.c: */
int32 resolve __ARGS((char *name));
char *resolve_a __ARGS((int32 ip_address));

/* In netuser.c: */
int32 aton __ARGS((char *s));
char *inet_ntoa __ARGS((int32 a));
char *pinet __ARGS((struct socket *s));

#endif  /* NCONN */
