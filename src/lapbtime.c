/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/lapbtime.c,v 1.2 1993-02-23 21:34:11 deyke Exp $ */

/* LAPB (AX25) timer recovery routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "timer.h"
#include "lapb.h"

static void tx_enq __ARGS((struct ax25_cb *axp));

/* Called whenever timer T1 expires */
void
recover(p)
void *p;
{
	register struct ax25_cb *axp = (struct ax25_cb *)p;

	axp->flags.retrans = 1;
	axp->retries++;
#if 0
	if((1L << axp->retries) < Blimit)
#endif
		/* Back off retransmit timer */
		set_timer(&axp->t1,(dur_timer(&axp->t1)*5+2)/4);
	if(axp->maxframe > 1)
		axp->maxframe--;

	switch(axp->state){
	case LAPB_SETUP:
		if(axp->peer && axp->peer->state == LAPB_DISCONNECTED){
			if(axp->retries > 2){
				free_q(&axp->txq);
				axp->reason = LB_TIMEOUT;
				lapbstate(axp,LAPB_DISCONNECTED);
			} else {
				start_timer(&axp->t1);
			}
		} else {
		if(axp->n2 != 0 && axp->retries > axp->n2){
			free_q(&axp->txq);
			axp->reason = LB_TIMEOUT;
			lapbstate(axp,LAPB_DISCONNECTED);
		} else {
			sendctl(axp,LAPB_COMMAND,SABM|PF);
			start_timer(&axp->t1);
		}
		}
		break;
	case LAPB_DISCPENDING:
		if(axp->n2 != 0 && axp->retries > axp->n2){
			axp->reason = LB_TIMEOUT;
			lapbstate(axp,LAPB_DISCONNECTED);
		} else {
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
		}
		break;
	case LAPB_CONNECTED:
	case LAPB_RECOVERY:
		if(axp->n2 != 0 && axp->retries > axp->n2){
			/* Give up */
			sendctl(axp,LAPB_RESPONSE,DM|PF);
			free_q(&axp->txq);
			axp->reason = LB_TIMEOUT;
			lapbstate(axp,LAPB_DISCONNECTED);
		} else {
			/* Transmit poll */
			tx_enq(axp);
			lapbstate(axp,LAPB_RECOVERY);
		}
		break;
	}
}

/* Send a poll (S-frame command with the poll bit set) */
void
pollthem(p)
void *p;
{
	register struct ax25_cb *axp;

	axp = (struct ax25_cb *)p;
	if(axp->proto == V1)
		return; /* Not supported in the old protocol */
	switch(axp->state){
	case LAPB_CONNECTED:
		axp->retries = 0;
		tx_enq(axp);
		lapbstate(axp,LAPB_RECOVERY);
		break;
	}
}
/* Transmit query */
static void
tx_enq(axp)
register struct ax25_cb *axp;
{
	char ctl;
	struct mbuf *bp;

	/* I believe that retransmitting the oldest unacked
	 * I-frame tends to give better performance than polling,
	 * as long as the frame isn't too "large", because
	 * chances are that the I frame got lost anyway.
	 * This is an option in LAPB, but not in the official AX.25.
	 */
	if(axp->txq != NULLBUF
	 && (len_p(axp->txq) < axp->pthresh || axp->proto == V1)){
		/* Retransmit oldest unacked I-frame */
		dup_p(&bp,axp->txq,0,len_p(axp->txq));
		ctl = PF | I | (((axp->vs - axp->unack) & MMASK) << 1)
		 | (axp->vr << 5);
		sendframe(axp,LAPB_COMMAND,ctl,bp);
	} else {
		ctl = busy(axp)                      ? RNR|PF : RR|PF;
		sendctl(axp,LAPB_COMMAND,ctl);
	}
	axp->response = 0;
	stop_timer(&axp->t3);
	start_timer(&axp->t1);
}
