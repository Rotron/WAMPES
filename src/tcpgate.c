/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/tcpgate.c,v 1.5 1990-10-12 19:26:43 deyke Exp $ */

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "hpux.h"

extern struct sockaddr *build_sockaddr();

/*---------------------------------------------------------------------------*/

static void tcp_send(tcb)
struct tcb *tcb;
{

  int  cnt, fd;
  struct mbuf *bp;

  fd = tcb->user;
  if ((cnt = space_tcp(tcb)) <= 0) {
    clrmask(chkread, fd);
    return;
  }
  if (!(bp = alloc_mbuf(cnt))) return;
  cnt = doread(fd, bp->data, (unsigned) cnt);
  if (cnt <= 0) {
    free_p(bp);
    clrmask(chkread, fd);
    close_tcp(tcb);
    return;
  }
  bp->cnt = cnt;
  send_tcp(tcb, bp);
}

/*---------------------------------------------------------------------------*/

static void tcp_receive(tcb, cnt)
struct tcb *tcb;
int16 cnt;
{

  char  buffer[1024];
  int  fd;
  struct mbuf *bp;

  fd = tcb->user;
  recv_tcp(tcb, &bp, 0);
  while ((cnt = pullup(&bp, buffer, sizeof(buffer))) > 0)
    if (dowrite(fd, buffer, (unsigned) cnt) < 0) close_tcp(tcb);
}

/*---------------------------------------------------------------------------*/

static void tcp_ready(tcb, cnt)
struct tcb *tcb;
int16 cnt;
{
  int  fd;

  fd = tcb->user;
  setmask(chkread, fd);
}

/*---------------------------------------------------------------------------*/

static void tcp_state(tcb, old, new)
struct tcb *tcb;
char  old, new;
{

  int  addrlen, fd;
  struct sockaddr *addr;

  switch (new) {
#ifdef  QUICKSTART
  case TCP_SYN_RECEIVED:
#else
  case TCP_ESTABLISHED:
#endif
    log(tcb, "open %s", tcp_port_name(tcb->conn.local.port));
    if (!(addr = build_sockaddr((char *) tcb->user, &addrlen)) ||
	(fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) {
      close_tcp(tcb);
      return;
    }
    tcb->user = fd;
    if (connect(fd, addr, addrlen)) {
      close_tcp(tcb);
      return;
    }
    setmask(chkread, fd);
    readfnc[fd] = tcp_send;
    readarg[fd] = (char *) tcb;
    return;
  case TCP_CLOSE_WAIT:
    close_tcp(tcb);
    return;
  case TCP_CLOSED:
    if (old == TCP_LISTEN) {
      free((void *) tcb->user);
      del_tcp(tcb);
      return;
    }
    log(tcb, "close %s", tcp_port_name(tcb->conn.local.port));
    fd = tcb->user;
    if (fd > 0 && fd < _NFILE) {
      clrmask(chkread, fd);
      readfnc[fd] = (void (*)()) 0;
      readarg[fd] = (char *) 0;
      close(fd);
    }
    del_tcp(tcb);
    return;
  }
}

/*---------------------------------------------------------------------------*/

int  tcpgate1(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  char  *socketname;
  char  buf[80];
  struct socket lsocket;

  lsocket.address = INADDR_ANY;
  lsocket.port = tcp_port_number(argv[1]);
  if (argc < 3)
    sprintf(socketname = buf, "loopback:%d", lsocket.port);
  else
    socketname = argv[2];
  socketname = strcpy(malloc((unsigned) (strlen(socketname) + 1)), socketname);
  open_tcp(&lsocket, NULLSOCK, TCP_SERVER, 0, tcp_receive, tcp_ready, tcp_state, 0, (int) socketname);
  return 0;
}

