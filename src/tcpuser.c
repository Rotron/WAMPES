/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/tcpuser.c,v 1.9 1991-02-24 20:17:50 deyke Exp $ */

/* User calls to TCP
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "socket.h"
#include "internet.h"
#include "tcp.h"
#include "ip.h"
#include "icmp.h"

int16 Tcp_window = DEF_WND;

struct tcb *
open_tcp(lsocket,fsocket,mode,window,r_upcall,t_upcall,s_upcall,tos,user)
struct socket *lsocket; /* Local socket */
struct socket *fsocket; /* Remote socket */
int mode;               /* Active/passive/server */
int16 window;           /* Receive window (and send buffer) sizes */
void (*r_upcall)();     /* Function to call when data arrives */
void (*t_upcall)();     /* Function to call when ok to send more data */
void (*s_upcall)();     /* Function to call when connection state changes */
int tos;
int user;               /* User linkage area */
{
	struct connection conn;
	register struct tcb *tcb;

	if(lsocket == NULLSOCK){
		Net_error = INVALID;
		return NULLTCB;
	}
	conn.local.address = lsocket->address;
	conn.local.port = lsocket->port;
	if(fsocket != NULLSOCK){
		conn.remote.address = fsocket->address;
		conn.remote.port = fsocket->port;
	} else {
		conn.remote.address = 0;
		conn.remote.port = 0;
	}
	if((tcb = lookup_tcb(&conn)) == NULLTCB){
		if((tcb = create_tcb(&conn)) == NULLTCB){
			Net_error = NO_MEM;
			return NULLTCB;
		}
	} else if(tcb->state != TCP_LISTEN){
		Net_error = CON_EXISTS;
		return NULLTCB;
	}
	tcb->user = user;
	if(window != 0)
		tcb->window = tcb->rcv.wnd = window;
	else
		tcb->window = tcb->rcv.wnd = Tcp_window;
	tcb->snd.wnd = 1;       /* Allow space for sending a SYN */
	tcb->r_upcall = r_upcall;
	tcb->t_upcall = t_upcall;
	tcb->s_upcall = s_upcall;
	tcb->tos = tos;
	switch(mode){
	case TCP_SERVER:
		tcb->flags.clone = 1;
	case TCP_PASSIVE:       /* Note fall-thru */
		setstate(tcb,TCP_LISTEN);
		break;
	case TCP_ACTIVE:
		/* Send SYN, go into TCP_SYN_SENT state */
		tcb->flags.active = 1;
		send_syn(tcb);
		setstate(tcb,TCP_SYN_SENT);
		tcp_output(tcb);
		break;
	}
	return tcb;
}
/* User send routine */
int
send_tcp(tcb,bp)
register struct tcb *tcb;
struct mbuf *bp;
{
	int16 cnt;

	if(tcb == NULLTCB || bp == NULLBUF){
		free_p(bp);
		Net_error = INVALID;
		return -1;
	}
	cnt = len_p(bp);
	switch(tcb->state){
	case TCP_CLOSED:
		free_p(bp);
		Net_error = NO_CONN;
		return -1;
	case TCP_LISTEN:
		if(tcb->conn.remote.address == 0 && tcb->conn.remote.port == 0){
			/* Save data for later */
			append(&tcb->sndq,bp);
			tcb->sndcnt += cnt;
			break;
		}
		/* Change state from passive to active */
		tcb->flags.active = 1;
		send_syn(tcb);
		setstate(tcb,TCP_SYN_SENT);     /* Note fall-thru */
	case TCP_SYN_SENT:
	case TCP_SYN_RECEIVED:
	case TCP_ESTABLISHED:
	case TCP_CLOSE_WAIT:
		append(&tcb->sndq,bp);
		tcb->sndcnt += cnt;
		tcp_output(tcb);
		break;
	case TCP_FINWAIT1:
	case TCP_FINWAIT2:
	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		free_p(bp);
		Net_error = CON_CLOS;
		return -1;
	}
	return (int)cnt;
}

/*---------------------------------------------------------------------------*/

int  space_tcp(tcb)
struct tcb *tcb;
{
  int  cnt;

  if (!tcb) {
    Net_error = INVALID;
    return (-1);
  }
  switch (tcb->state) {
  case TCP_CLOSED:
    Net_error = NO_CONN;
    return (-1);
  case TCP_LISTEN:
  case TCP_SYN_SENT:
  case TCP_SYN_RECEIVED:
  case TCP_ESTABLISHED:
  case TCP_CLOSE_WAIT:
    cnt = tcb->window - tcb->sndcnt;
    return (cnt > 0) ? cnt : 0;
  case TCP_FINWAIT1:
  case TCP_FINWAIT2:
  case TCP_CLOSING:
  case TCP_LAST_ACK:
  case TCP_TIME_WAIT:
    Net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

/* User receive routine */
int
recv_tcp(tcb,bpp,cnt)
register struct tcb *tcb;
struct mbuf **bpp;
int16 cnt;
{
	if(tcb == NULLTCB || bpp == (struct mbuf **)NULL){
		Net_error = INVALID;
		return -1;
	}
	if(tcb->rcvcnt == 0){
		/* If there's nothing on the queue, our action depends on what state
		 * we're in (i.e., whether or not we're expecting any more data).
		 * If no more data is expected, then simply return 0; this is
		 * interpreted as "end of file". Otherwise return -1.
		 */
		switch(tcb->state){
		case TCP_LISTEN:
		case TCP_SYN_SENT:
		case TCP_SYN_RECEIVED:
		case TCP_ESTABLISHED:
		case TCP_FINWAIT1:
		case TCP_FINWAIT2:
			Net_error = WOULDBLK;
			return -1;
		case TCP_CLOSED:
		case TCP_CLOSE_WAIT:
		case TCP_CLOSING:
		case TCP_LAST_ACK:
		case TCP_TIME_WAIT:
			*bpp = NULLBUF;
			return 0;
		}
	}
	/* cnt == 0 means "I want it all" */
	if(cnt == 0)
		cnt = tcb->rcvcnt;
	/* See if the user can take all of it */
	if(tcb->rcvcnt <= cnt){
		cnt = tcb->rcvcnt;
		*bpp = tcb->rcvq;
		tcb->rcvq = NULLBUF;
	} else {
		*bpp = ambufw(cnt);
		pullup(&tcb->rcvq,(*bpp)->data,cnt);
		(*bpp)->cnt = cnt;
	}
	tcb->rcvcnt -= cnt;
	tcb->rcv.wnd += cnt;
	/* Do a window update if it was closed */
	if(cnt == tcb->rcv.wnd){
		tcb->flags.force = 1;
		tcp_output(tcb);
	}
	return (int)cnt;
}
/* This really means "I have no more data to send". It only closes the
 * connection in one direction, and we can continue to receive data
 * indefinitely.
 */
int
close_tcp(tcb)
register struct tcb *tcb;
{
	if(tcb == NULLTCB){
		Net_error = INVALID;
		return -1;
	}
	switch(tcb->state){
	case TCP_CLOSED:
		return 0;       /* Unlikely */
	case TCP_LISTEN:
	case TCP_SYN_SENT:
		close_self(tcb,NORMAL);
		return 0;
	case TCP_SYN_RECEIVED:
	case TCP_ESTABLISHED:
		tcb->sndcnt++;
		tcb->snd.nxt++;
		setstate(tcb,TCP_FINWAIT1);
		tcp_output(tcb);
		return 0;
	case TCP_CLOSE_WAIT:
		tcb->sndcnt++;
		tcb->snd.nxt++;
		setstate(tcb,TCP_LAST_ACK);
		tcp_output(tcb);
		return 0;
	case TCP_FINWAIT1:
	case TCP_FINWAIT2:
	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		Net_error = CON_CLOS;
		return -1;
	}
	return -1;      /* "Can't happen" */
}
/* Delete TCB, free resources. The user is not notified, even if the TCB is
 * not in the TCP_CLOSED state. This function should normally be called by the
 * user only in response to a state change upcall to TCP_CLOSED state.
 */
int
del_tcp(tcb)
register struct tcb *tcb;
{
	struct reseq *rp,*rp1;

	if(tcb == NULLTCB){
		Net_error = INVALID;
		return -1;
	}
	unlink_tcb(tcb);
	stop_timer(&tcb->timer);
	for(rp = tcb->reseq;rp != NULLRESEQ;rp = rp1){
		rp1 = rp->next;
		free_p(rp->bp);
		free((char *)rp);
	}
	tcb->reseq = NULLRESEQ;
	free_p(tcb->rcvq);
	free_p(tcb->sndq);
	free((char *)tcb);
	return 0;
}
/* Return 1 if arg is a valid TCB, 0 otherwise */
int
tcpval(tcb)
struct tcb *tcb;
{
	register int i;
	register struct tcb *tcb1;

	if(tcb == NULLTCB)
		return 0;       /* Null pointer can't be valid */
	for(i=0;i<NTCB;i++){
		for(tcb1=Tcbs[i];tcb1 != NULLTCB;tcb1 = tcb1->next){
			if(tcb1 == tcb)
				return 1;
		}
	}
	return 0;
}
/* Kick a particular TCP connection */
int
kick_tcp(tcb)
register struct tcb *tcb;
{
	if(!tcpval(tcb))
		return -1;
	tcp_timeout(tcb);
	return 0;
}
/* Kick all TCP connections to specified address; return number kicked */
int
kick(addr)
int32 addr;
{
	register int i;
	register struct tcb *tcb;
	int cnt = 0;

	for(i=0;i<NTCB;i++){
		for(tcb=Tcbs[i];tcb != NULLTCB;tcb = tcb->next){
			if(tcb->conn.remote.address == addr){
				kick_tcp(tcb);
				cnt++;
			}
		}
	}
	return cnt;
}
/* Clear all TCP connections */
void
reset_all()
{
	register int i;
	register struct tcb *tcb;

	for(i=0;i<NTCB;i++){
		for(tcb=Tcbs[i];tcb != NULLTCB;tcb = tcb->next)
			reset_tcp(tcb);
	}
}
void
reset_tcp(tcb)
register struct tcb *tcb;
{
	struct tcp fakeseg;
	struct ip fakeip;

	if(tcb == NULLTCB)
		return;
	if(tcb->state != TCP_LISTEN){
		/* Compose a fake segment with just enough info to generate the
		 * correct RST reply
		 */
		fakeseg.flags.rst = 0;
		fakeseg.dest = tcb->conn.local.port;
		fakeseg.source = tcb->conn.remote.port;
		fakeseg.flags.ack = 1;
		/* Here we try to pick a sequence number with the greatest likelihood
		 * of being in his receive window.
		 */
		fakeseg.ack = tcb->snd.nxt + tcb->snd.wnd - 1;
		fakeip.dest = tcb->conn.local.address;
		fakeip.source = tcb->conn.remote.address;
		fakeip.tos = tcb->tos;
		reset(&fakeip,&fakeseg);
	}
	close_self(tcb,RESET);
}
#ifdef  notused
/* Return character string corresponding to a TCP well-known port, or
 * the decimal number if unknown.
 */
char *
tcp_port(n)
int16 n;
{
	static char buf[32];

	switch(n){
	case IPPORT_ECHO:
		return "echo";
	case IPPORT_DISCARD:
		return "discard";
	case IPPORT_FTPD:
		return "ftp_data";
	case IPPORT_FTP:
		return "ftp";
	case IPPORT_TELNET:
		return "telnet";
	case IPPORT_SMTP:
		return "smtp";
	default:
		sprintf(buf,"%u",n);
		return buf;
	}
}
#endif

