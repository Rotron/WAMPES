/* @(#) $Id: netuser.h,v 1.14 1999-01-27 18:45:40 deyke Exp $ */

#ifndef _NETUSER_H
#define _NETUSER_H

/* Global structures and constants needed by an Internet user process */

#ifndef _GLOBAL_H
#include "global.h"
#endif

extern int32 Ip_addr;   /* Our IP address */
extern int Net_error;   /* Error return code */
extern char Inet_eol[];

#define NONE            0       /* No error */
#define CON_EXISTS      1       /* Connection already exists */
#define NO_CONN         2       /* Connection does not exist */
#define CON_CLOS        3       /* Connection closing */
#define NO_MEM          4       /* No memory for TCB creation */
#define WOULDBLK        5       /* Would block */
#define NOPROTO         6       /* Protocol or mode not supported */
#define INVALID         7       /* Invalid arguments */

/* Codes for the tcp_open call */
#define TCP_PASSIVE     0
#define TCP_ACTIVE      1
#define TCP_SERVER      2       /* Passive, clone on opening */

/* Local IP wildcard address */
#ifndef INADDR_ANY
#define INADDR_ANY      0x0L
#endif

#define NET_HDR_PAD     70      /* mbuf size to preallocate for headers */

/* Socket structure */
struct socket {
	int32 address;          /* IP address */
	uint port;              /* port number */
};

/* Connection structure (two sockets) */
struct connection {
	struct socket local;
	struct socket remote;
};
/* In domain.c: */
int32 resolve(char *name);
int32 resolve_mx(char *name);
char *resolve_a(int32 ip_address, int shorten);

/* In netuser.c: */
int32 aton(char *s);
char *inet_ntoa(int32 a);
char *pinet(struct socket *s);

/* In services.c: */
char *tcp_port_name(int port);
char *udp_port_name(int port);
int tcp_port_number(char *name);
int udp_port_number(char *name);
char *pinet_tcp(struct socket *s);
char *pinet_udp(struct socket *s);

#endif  /* _NETUSER_H */
