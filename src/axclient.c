/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/axclient.c,v 1.12 1993-02-23 21:34:04 deyke Exp $ */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "session.h"

static void axclient_parse __ARGS((char *buf, int n));
static void axclient_state_upcall __ARGS((struct ax25_cb *cp, int oldstate, int newstate));

/*---------------------------------------------------------------------------*/

static void axclient_parse(buf, n)
char *buf;
int n;
{
  if (!(Current && Current->type == AX25TNC && Current->cb.ax25)) return;
  if (n >= 1 && buf[n-1] == '\n') n--;
  if (!n) return;
  send_ax25(Current->cb.ax25, qdata(buf, n), PID_NO_L3);
  if (Current->record) {
    if (buf[n-1] == '\r') buf[n-1] = '\n';
    fwrite(buf, 1, n, Current->record);
  }
}

/*---------------------------------------------------------------------------*/

void axclient_send_upcall(cp, cnt)
struct ax25_cb *cp;
int cnt;
{

  char *p;
  int chr;
  struct mbuf *bp;
  struct session *s;

  if (!(s = (struct session *) cp->user) || !s->upload || cnt <= 0) return;
  if (!(bp = alloc_mbuf(cnt))) return;
  p = bp->data;
  while (cnt) {
    if ((chr = getc(s->upload)) == EOF) break;
    if (chr == '\n') chr = '\r';
    *p++ = chr;
    cnt--;
  }
  if (bp->cnt = p - bp->data)
    send_ax25(cp, bp, PID_NO_L3);
  else
    free_p(bp);
  if (cnt) {
    fclose(s->upload);
    s->upload = 0;
    free(s->ufile);
    s->ufile = 0;
  }
}

/*---------------------------------------------------------------------------*/

void axclient_recv_upcall(cp, cnt)
struct ax25_cb *cp;
int cnt;
{

  int c;
  struct mbuf *bp;

  if (!(Mode == CONV_MODE && Current && Current->type == AX25TNC && Current->cb.ax25 == cp)) return;
  bp = recv_ax25(cp, 0);
  while ((c = PULLCHAR(&bp)) != -1) {
    if (c == '\r') c = '\n';
    putchar(c);
    if (Current->record) putc(c, Current->record);
  }
}

/*---------------------------------------------------------------------------*/

static void axclient_state_upcall(cp, oldstate, newstate)
struct ax25_cb *cp;
int oldstate, newstate;
{
  int notify;

  notify = (Current && Current->type == AX25TNC && Current == (struct session *) cp->user);
  if (newstate != LAPB_DISCONNECTED) {
    if (notify) printf("%s\n", Ax25states[newstate]);
  } else {
    if (notify) printf("%s (%s)\n", Ax25states[newstate], Axreasons[cp->reason]);
    if (cp->user) freesession((struct session *) cp->user);
    del_ax25(cp);
    if (notify) cmdmode();
  }
}

/*---------------------------------------------------------------------------*/

/* Initiate interactive AX.25 connect to remote station */
int
doconnect(argc,argv,p)
int argc;
char *argv[];
void *p;
{

  struct ax25 hdr;
  struct session *s;

  if (ax25args_to_hdr(argc - 1, argv + 1, &hdr)) return 1;
  if (!(s = newsession())) {
    printf("Too many sessions\n");
    return 1;
  }
  Current = s;
  s->type = AX25TNC;
  s->name = NULLCHAR;
  s->cb.ax25 = NULLAX25;
  s->parse = axclient_parse;
  if (!(s->cb.ax25 = open_ax25(&hdr, AX_ACTIVE, axclient_recv_upcall, axclient_send_upcall, axclient_state_upcall, (char *) s))) {
    freesession(s);
    printf("connect failed\n");
    return 1;
  }
  go(argc, argv, p);
  return 0;
}

