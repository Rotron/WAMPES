/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/axserver.c,v 1.4 1990-09-11 13:45:08 deyke Exp $ */

#include <stdlib.h>

#include "global.h"
#include "timer.h"
#include "axproto.h"
#include "login.h"

static void axserv_recv_upcall __ARGS((struct axcb *cp, int cnt));
static void axserv_send_upcall __ARGS((struct axcb *cp, int cnt));
static void axserv_state_upcall __ARGS((struct axcb *cp, int oldstate, int newstate));

/*---------------------------------------------------------------------------*/

static void axserv_recv_upcall(cp, cnt)
struct axcb *cp;
int  cnt;
{
  struct mbuf *bp;

  recv_ax(cp, &bp, 0);
  login_write((struct login_cb *) cp->user, bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_send_upcall(cp, cnt)
struct axcb *cp;
int  cnt;
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
    cp->user = (char *) login_open(pathtostr(cp), "AX25", (void (*)()) axserv_send_upcall, (void (*)()) close_ax, cp);
    if (!cp->user) close_ax(cp);
    break;
  case DISCONNECTED:
    login_close((struct login_cb *) cp->user);
    del_ax(cp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

int  ax250(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  if (axcb_server) {
    free((char *) axcb_server);
    axcb_server = NULLAXCB;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int  ax25start(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  if (!axcb_server)
    axcb_server = open_ax(NULLCHAR, AX25_SERVER, axserv_recv_upcall,
			  axserv_send_upcall, axserv_state_upcall, NULLCHAR);
  return axcb_server ? 0 : -1;
}

