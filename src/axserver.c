/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/axserver.c,v 1.3 1990-08-23 17:32:40 deyke Exp $ */

#include <stdlib.h>

#include "global.h"
#include "timer.h"
#include "axproto.h"
#include "login.h"

/*---------------------------------------------------------------------------*/

static void axserv_recv_upcall(cp)
struct axcb *cp;
{
  struct mbuf *bp;

  recv_ax(cp, &bp, 0);
  login_write((struct login_cb *) cp->user, bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_send_upcall(cp)
struct axcb *cp;
{
  struct mbuf *bp;

  if (bp = login_read((struct login_cb *) cp->user, space_ax(cp)))
    send_ax(cp, bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_state_upcall(cp, oldstate, newstate)
struct axcb *cp;
int  oldstate, newstate;
{
  switch (newstate) {
  case CONNECTED:
    cp->user = (char *) login_open(pathtostr(cp), "AX25", axserv_send_upcall, close_ax, (char *) cp);
    if (!cp->user) close_ax(cp);
    break;
  case DISCONNECTED:
    login_close((struct login_cb *) cp->user);
    del_ax(cp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

int axserv_stop()
{
  if (!axcb_server) return (-1);
  free((char *) axcb_server);
  axcb_server = NULLAXCB;
  return 0;
}

/*---------------------------------------------------------------------------*/

int axserv_start(argc, argv)
int  argc;
char  *argv[];
{
  axserv_stop();
  axcb_server = open_ax(NULLCHAR, AX25_SERVER, axserv_recv_upcall, axserv_send_upcall, axserv_state_upcall, NULLCHAR);
  return axcb_server ? 0 : -1;
}

