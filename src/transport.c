/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/transport.c,v 1.10 1991-12-04 18:26:00 deyke Exp $ */

#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "tcp.h"
#include "session.h"
#include "transport.h"

static int convert_eol __ARGS((struct mbuf **bpp, int mode, int *last_chr));
static void transport_recv_upcall_ax25 __ARGS((struct ax25_cb *cp, int cnt));
static void transport_recv_upcall_netrom __ARGS((struct circuit *cp, int cnt));
static void transport_recv_upcall_tcp __ARGS((struct tcb *cp, int cnt));
static void transport_send_upcall_ax25 __ARGS((struct ax25_cb *cp, int cnt));
static void transport_send_upcall_netrom __ARGS((struct circuit *cp, int cnt));
static void transport_send_upcall_tcp __ARGS((struct tcb *cp, int cnt));
static void transport_state_upcall_ax25 __ARGS((struct ax25_cb *cp, int oldstate, int newstate));
static void transport_state_upcall_netrom __ARGS((struct circuit *cp, int oldstate, int newstate));
static void transport_state_upcall_tcp __ARGS((struct tcb *cp, int oldstate, int newstate));
static struct ax25_cb *transport_open_ax25 __ARGS((char *address, struct transport_cb *tp));
static struct circuit *transport_open_netrom __ARGS((char *address, struct transport_cb *tp));
static struct tcb *transport_open_tcp __ARGS((char *address, struct transport_cb *tp));

/*---------------------------------------------------------------------------*/

static int  convert_eol(bpp, mode, last_chr)
struct mbuf **bpp;
int  mode, *last_chr;
{

  char  buf[10240], *p;
  int  chr, cnt;
  struct mbuf *bp;

  p = buf;
  bp = *bpp;
  while (bp) {
    for (; bp->cnt; bp->cnt--) {
      switch (chr = *bp->data++) {
      case '\0':
	break;
      case '\n':
	if (*last_chr == '\r')
	  break;
      case '\r':
	switch (mode) {
	case EOL_CR:
	  *p++ = '\r';
	  break;
	case EOL_LF:
	  *p++ = '\n';
	  break;
	case EOL_CRLF:
	  *p++ = '\r';
	  *p++ = '\n';
	  break;
	}
	break;
      default:
	*p++ = chr;
	break;
      }
      *last_chr = chr;
    }
    bp = free_mbuf(bp);
  }
  cnt = p - buf;
  *bpp = qdata(buf, cnt);
  return cnt;
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_ax25(cp, cnt)
struct ax25_cb *cp;
int cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_netrom(cp, cnt)
struct circuit *cp;
int cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_tcp(cp, cnt)
struct tcb *cp;
int cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_ax25(cp, cnt)
struct ax25_cb *cp;
int cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_netrom(cp, cnt)
struct circuit *cp;
int cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_tcp(cp, cnt)
struct tcb *cp;
int cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_ax25(cp, oldstate, newstate)
struct ax25_cb *cp;
int  oldstate, newstate;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->s_upcall && newstate == DISCONNECTED) (*tp->s_upcall)(tp);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_netrom(cp, oldstate, newstate)
struct circuit *cp;
int  oldstate, newstate;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->s_upcall && newstate == DISCONNECTED) (*tp->s_upcall)(tp);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_tcp(cp, oldstate, newstate)
struct tcb *cp;
int oldstate, newstate;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  switch (newstate) {
  case TCP_CLOSE_WAIT:
    close_tcp(cp);
    break;
  case TCP_CLOSED:
    if (tp->s_upcall) (*tp->s_upcall)(tp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static struct ax25_cb *transport_open_ax25(address, tp)
char  *address;
struct transport_cb *tp;
{

  char  *pathptr;
  char  *strptr;
  char  path[10*AXALEN];
  char  tmp[1024];

  pathptr = path;
  strptr = strtok(strcpy(tmp, address), " \t\r\n");
  while (strptr) {
    if (strncmp("via", strptr, strlen(strptr))) {
      if (pathptr > path + 10 * AXALEN - 1) return 0;
      if (setcall(pathptr, strptr)) return 0;
      if (pathptr == path) {
	pathptr += AXALEN;
	addrcp(pathptr, Mycall);
      }
      pathptr += AXALEN;
    }
    strptr = strtok(NULLCHAR, " \t\r\n");
  }
  if (pathptr < path + 2 * AXALEN) return 0;
  pathptr[-1] |= E;
  tp->recv = (int (*)()) recv_ax;
  tp->send = (int (*)()) send_ax;
  tp->send_space = (int (*)()) space_ax;
  tp->close = (int (*)()) close_ax;
  tp->del = (int (*)()) del_ax;
  return open_ax(path, AX_ACTIVE, transport_recv_upcall_ax25, transport_send_upcall_ax25, transport_state_upcall_ax25, (char *) tp);
}

/*---------------------------------------------------------------------------*/

static struct circuit *transport_open_netrom(address, tp)
char *address;
struct transport_cb *tp;
{

  char *ascii_node, *ascii_user;
  char node[AXALEN], user[AXALEN];
  char tmp[1024];

  strcpy(tmp, address);
  if (!(ascii_node = strtok(tmp, " \t\r\n")) ||
      setcall(node, ascii_node))
    return 0;
  if (!(ascii_user = strtok(NULLCHAR, " \t\r\n")) ||
      setcall(user, ascii_user))
    addrcp(user, Mycall);
  tp->recv = (int (*)()) recv_nr;
  tp->send = (int (*)()) send_nr;
  tp->send_space = (int (*)()) space_nr;
  tp->close = (int (*)()) close_nr;
  tp->del = (int (*)()) del_nr;
  return open_nr(node, user, 0, transport_recv_upcall_netrom, transport_send_upcall_netrom, transport_state_upcall_netrom, (char *) tp);
}

/*---------------------------------------------------------------------------*/

static struct tcb *transport_open_tcp(address, tp)
char  *address;
struct transport_cb *tp;
{

  char  *host, *port, tmp[1024];
  struct socket fsocket, lsocket;

  if (!(host = strtok(strcpy(tmp, address), " \t\r\n"))) return 0;
  if (!(port = strtok(NULLCHAR, " \t\r\n"))) return 0;
  if (!(fsocket.address = resolve(host))) return 0;
  if (!(fsocket.port = tcp_port_number(port))) return 0;
  lsocket.address = INADDR_ANY;
  lsocket.port = Lport++;
  tp->recv = (int (*)()) recv_tcp;
  tp->send = (int (*)()) send_tcp;
  tp->send_space = (int (*)()) space_tcp;
  tp->close = (int (*)()) close_tcp;
  tp->del = (int (*)()) del_tcp;
  return open_tcp(&lsocket, &fsocket, TCP_ACTIVE, 0, transport_recv_upcall_tcp, transport_send_upcall_tcp, transport_state_upcall_tcp, 0, (int) tp);
}

/*---------------------------------------------------------------------------*/

struct transport_cb *transport_open(protocol, address, r_upcall, t_upcall, s_upcall, user)
char *protocol;
char *address;
void (*r_upcall) __ARGS((struct transport_cb *tp, int cnt));
void (*t_upcall) __ARGS((struct transport_cb *tp, int cnt));
void (*s_upcall) __ARGS((struct transport_cb *tp));
void *user;
{
  struct transport_cb *tp;

  tp = calloc(1, sizeof(struct transport_cb ));
  tp->r_upcall = r_upcall;
  tp->t_upcall = t_upcall;
  tp->s_upcall = s_upcall;
  tp->user = user;
  tp->timer.func = (void (*)()) transport_close;
  tp->timer.arg = tp;
  Net_error = INVALID;
  if (!strcmp(protocol, "ax25"))
    tp->cp = transport_open_ax25(address, tp);
  else if (!strcmp(protocol, "netrom"))
    tp->cp = transport_open_netrom(address, tp);
  else if (!strcmp(protocol, "tcp"))
    tp->cp = transport_open_tcp(address, tp);
  else
    Net_error = NOPROTO;
  if (tp->cp) return tp;
  free(tp);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  transport_recv(tp, bpp, cnt)
struct transport_cb *tp;
struct mbuf **bpp;
int cnt;
{
  int  result;

  if dur_timer(&tp->timer) start_timer(&tp->timer);
  result = (*tp->recv)(tp->cp, bpp, cnt);
  if (result < 1 || tp->recv_mode == EOL_NONE) return result;
  return convert_eol(bpp, tp->recv_mode, &tp->recv_char);
}

/*---------------------------------------------------------------------------*/

int  transport_send(tp, bp)
struct transport_cb *tp;
struct mbuf *bp;
{
  if dur_timer(&tp->timer) start_timer(&tp->timer);
  if (tp->send_mode != EOL_NONE)
    convert_eol(&bp, tp->send_mode, &tp->send_char);
  return (*tp->send)(tp->cp, bp);
}

/*---------------------------------------------------------------------------*/

int  transport_send_space(tp)
struct transport_cb *tp;
{
  return (*tp->send_space)(tp->cp);
}

/*---------------------------------------------------------------------------*/

void transport_set_timeout(tp, timeout)
struct transport_cb *tp;
int  timeout;
{
  set_timer(&tp->timer, timeout * 1000L);
  start_timer(&tp->timer);
}

/*---------------------------------------------------------------------------*/

int  transport_close(tp)
struct transport_cb *tp;
{
  return (*tp->close)(tp->cp);
}

/*---------------------------------------------------------------------------*/

int  transport_del(tp)
struct transport_cb *tp;
{
  (*tp->del)(tp->cp);
  stop_timer(&tp->timer);
  free(tp);
  return 0;
}

