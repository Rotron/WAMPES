static char  rcsid[] = "@(#) $Header: /home/deyke/tmp/cvs/tcp/util/bridge.c,v 1.8 1991-09-23 17:13:35 deyke Exp $";

#define _HPUX_SOURCE

#include <sys/types.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef __STDC__
#define __ARGS(x)       x
#else
#define __ARGS(x)       ()
#endif

struct sockaddr *build_sockaddr __ARGS((char *name, int *addrlen));

/* SLIP constants */

#define FR_END          0300    /* Frame End */
#define FR_ESC          0333    /* Frame Escape */
#define T_FR_END        0334    /* Transposed frame end */
#define T_FR_ESC        0335    /* Transposed frame escape */

/* KISS constants */

#define KISS_DATA       0

/* AX25 constants */

#define AXALEN          7       /* Total AX.25 address length, including SSID */
#define E               0x01    /* Address extension bit */
#define REPEATED        0x80    /* Has-been-repeated bit in repeater field */
#define SSID            0x1e    /* Sub station ID */

#define uchar(c)        ((unsigned char) (c))

struct conn {
  struct conn *prev, *next;
  char  call[AXALEN];
  int  fd;
  int  mask;
  unsigned int  cnt;
  char  buf[4096];
};

/* AX.25 broadcast address: "QST-0" in shifted ascii */
static char  ax25_bdcst[] = {
  'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, ('0' << 1) | E,
};

/* NET/ROM broadcast address: "NODES-0" in shifted ascii */
static char  nr_bdcst[] = {
  'N' << 1, 'O' << 1, 'D' << 1, 'E' << 1, 'S' << 1, ' ' << 1, ('0' << 1) | E
};

static int  filemask;
static struct conn *connections;

static void sigpipe_handler __ARGS((int sig, int code, struct sigcontext *scp));
static void create_conn __ARGS((int flisten));
static void close_conn __ARGS((struct conn *p));
static int addreq __ARGS((char *a, char *b));
static void route_packet __ARGS((struct conn *p));

/*---------------------------------------------------------------------------*/

static void sigpipe_handler(sig, code, scp)
int  sig, code;
struct sigcontext *scp;
{
  scp->sc_syscall_action = SIG_RETURN;
}

/*---------------------------------------------------------------------------*/

static void create_conn(flisten)
int  flisten;
{

  int  fd, addrlen;
  struct conn *p;
  struct sockaddr addr;

  addrlen = 0;
  fd = accept(flisten, &addr, &addrlen);
  if (fd >= 0) {
    p = (struct conn *) calloc(1, sizeof(struct conn ));
    p->fd = fd;
    p->mask = (1 << p->fd);
    filemask |= p->mask;
    if (connections) {
      p->next = connections;
      connections->prev = p;
    }
    connections = p;
  }
}

/*---------------------------------------------------------------------------*/

static void close_conn(p)
struct conn *p;
{
  close(p->fd);
  filemask &= ~p->mask;
  if (p->prev) p->prev->next = p->next;
  if (p->next) p->next->prev = p->prev;
  if (p == connections) connections = p->next;
  free(p);
}

/*---------------------------------------------------------------------------*/

static int  addreq(a, b)
register char  *a, *b;
{
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  return (*a & SSID) == (*b & SSID);
}

/*---------------------------------------------------------------------------*/

static void route_packet(p)
struct conn *p;
{

  char  *ap;
  char  *dest;
  char  *src;
  int  multicast;
  struct conn *p1, *p1next;

  if ((*p->buf & 0xf) != KISS_DATA) return;
  dest = p->buf + 1;
  ap = src = dest + AXALEN;
  while (!(ap[6] & E)) {
    ap += AXALEN;
    if (ap[6] & REPEATED)
      src = ap;
    else {
      dest = ap;
      break;
    }
  }
  memcpy(p->call, src, AXALEN);
  multicast = (addreq(dest, ax25_bdcst) || addreq(dest, nr_bdcst));
  for (p1 = connections; p1; p1 = p1next) {
    p1next = p1->next;
    if (p1 != p && (multicast || !*p1->call || addreq(dest, p1->call))) {
      if (write(p1->fd, p->buf, p->cnt) <= 0) close_conn(p1);
    }
  }
}

/*---------------------------------------------------------------------------*/

int main()
{

  char buf[1024];
  int addrlen;
  int arg;
  int flisten, flistenmask;
  int i;
  int n;
  int readmask;
  struct conn *p;
  struct sigvec vec;
  struct sockaddr *addr;

  for (n = 0; n < _NFILE; n++) close(n);
  chdir("/");
  setpgrp();

  vec.sv_mask = vec.sv_flags = 0;
  vec.sv_handler = sigpipe_handler;
  sigvector(SIGPIPE, &vec, (struct sigvec *) 0);

  addr = build_sockaddr("*:4713", &addrlen);
  if (!addr) exit(1);
  flisten = socket(addr->sa_family, SOCK_STREAM, 0);
  if (flisten < 0) exit(1);
  arg = 1;
  setsockopt(flisten, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
  if (bind(flisten, addr, addrlen)) exit(1);
  if (listen(flisten, SOMAXCONN)) exit(1);
  filemask = flistenmask = (1 << flisten);

  for (; ; ) {
    readmask = filemask;
    if (select(32, &readmask, (int *) 0, (int *) 0, (struct timeval *) 0) <= 0)
      continue;
    if (readmask & flistenmask) create_conn(flisten);
    for (p = connections; p; p = p->next)
      if (readmask & p->mask) {
	n = read(p->fd, buf, sizeof(buf));
	if (n <= 0) {
	  close_conn(p);
	  break;
	}
	for (i = 0; i < n; i++)
	  if (uchar(p->buf[p->cnt++] = buf[i]) == FR_END) {
	    if (p->cnt > 1) route_packet(p);
	    p->cnt = 0;
	  }
      }
  }
}

