/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/tnserv.c,v 1.5 1990-10-12 19:26:55 deyke Exp $ */

#include "global.h"
#include "socket.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "login.h"

static struct tcb *tcb_server;

/*---------------------------------------------------------------------------*/

static void tnserv_recv_upcall(tcb, cnt)
struct tcb *tcb;
int  cnt;
{
  struct mbuf *bp;

  recv_tcp(tcb, &bp, 0);
  login_write((struct login_cb *) tcb->user, bp);
}

/*---------------------------------------------------------------------------*/

static void tnserv_send_upcall(tcb, cnt)
struct tcb *tcb;
int  cnt;
{
  struct mbuf *bp;

  if (bp = login_read((struct login_cb *) tcb->user, space_tcp(tcb)))
    send_tcp(tcb, bp);
}

/*---------------------------------------------------------------------------*/

static void tnserv_state_upcall(tcb, old, new)
struct tcb *tcb;
char  old, new;
{
  switch (new) {
#ifdef  QUICKSTART
  case TCP_SYN_RECEIVED:
#else
  case TCP_ESTABLISHED:
#endif
    tcb->user = (int) login_open(pinet_tcp(&tcb->conn.remote), "TELNET", (void (*)()) tnserv_send_upcall, (void (*)()) close_tcp, tcb);
    if (!tcb->user)
      close_tcp(tcb);
    else
      log(tcb, "open TELNET");
    break;
  case TCP_CLOSE_WAIT:
    close_tcp(tcb);
    break;
  case TCP_CLOSED:
    if (old != TCP_LISTEN) {
      login_close((struct login_cb *) tcb->user);
      log(tcb, "close TELNET");
    }
    del_tcp(tcb);
    if (tcb == tcb_server) tcb_server = NULLTCB;
    break;
  }
}

/*---------------------------------------------------------------------------*/

int  telnet0(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  if (tcb_server) close_tcp(tcb_server);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  telnet1(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  struct socket lsocket;

  if (tcb_server) close_tcp(tcb_server);
  lsocket.address = INADDR_ANY;
  lsocket.port = (argc < 2) ? IPPORT_TELNET : tcp_port_number(argv[1]);
  tcb_server = open_tcp(&lsocket, NULLSOCK, TCP_SERVER, 0, tnserv_recv_upcall,
			tnserv_send_upcall, tnserv_state_upcall, 0, 0);
  return 0;
}

