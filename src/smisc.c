/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/smisc.c,v 1.2 1990-04-12 17:51:57 deyke Exp $ */

/* Miscellaneous servers */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "remote.h"

extern long currtime;

char *Rempass = " ";    /* Remote access password */

static struct tcb *disc_tcb,*echo_tcb;
static struct socket remsock;

/* Start up discard server */
dis1(argc,argv)
int argc;
char *argv[];
{
	struct socket lsocket;
	void disc_recv(),misc_state();

	lsocket.address = ip_addr;
	if(argc < 2)
		lsocket.port = DISCARD_PORT;
	else
		lsocket.port = tcp_portnum(argv[1]);
	disc_tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,disc_recv,NULLVFP,misc_state,0,(char *)NULL);
}
/* Start echo server */
echo1(argc,argv)
int argc;
char *argv[];
{
	void echo_recv(),echo_trans(),misc_state();
	struct socket lsocket;

	lsocket.address = ip_addr;
	if(argc < 2)
		lsocket.port = ECHO_PORT;
	else
		lsocket.port = tcp_portnum(argv[1]);
	echo_tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,echo_recv,echo_trans,misc_state,0,(char *)NULL);

}

/* Start remote exit/reboot server */
rem1(argc,argv)
int argc;
char *argv[];
{
	void uremote();

	remsock.address = ip_addr;
	if(argc < 2)
		remsock.port = IPPORT_REMOTE;
	else
		remsock.port = atoi(argv[1]);
	open_udp(&remsock,uremote);
}

/* Shut down miscellaneous servers */
dis0()
{
	if(disc_tcb != NULLTCB)
		close_tcp(disc_tcb);
}
echo0()
{
	if(echo_tcb != NULLTCB)
		close_tcp(echo_tcb);
}
rem0()
{
	del_udp(&remsock);
}
/* Discard server receiver upcall */
static
void
disc_recv(tcb,cnt)
struct tcb *tcb;
int16 cnt;
{
	struct mbuf *bp;

	if(recv_tcp(tcb,&bp,cnt) > 0)
		free_p(bp);                     /* Discard */
}

/* Echo server receive
 * Copies only as much will fit on the transmit queue
 */
static
void
echo_recv(tcb,cnt)
struct tcb *tcb;
int cnt;
{
	struct mbuf *bp;
	int acnt;

	if(cnt == 0){
		close_tcp(tcb);
		return;
	}
	acnt = min(cnt,tcb->snd.wnd);
	if(acnt > 0){
		/* Get only as much will fit in the send window */
		recv_tcp(tcb,&bp,tcb->snd.wnd);
		send_tcp(tcb,bp);
	}
}
/* Echo server transmit
 * Copies anything that might have been left in the receiver queue
 */
static
void
echo_trans(tcb,cnt)
struct tcb *tcb;
int16 cnt;
{
	struct mbuf *bp;

	if(tcb->rcvcnt > 0){
		/* Get only as much will fit in the send window */
		recv_tcp(tcb,&bp,cnt);
		send_tcp(tcb,bp);
	}
}

/* Log connection state changes; also respond to remote closes */
static
void
misc_state(tcb,old,new)
register struct tcb *tcb;
char old,new;
{
	switch(new){
	case ESTABLISHED:
		log(tcb,"open %s",tcp_port(tcb->conn.local.port));
		break;
	case CLOSE_WAIT:
		close_tcp(tcb);
		break;
	case CLOSED:
		log(tcb,"close %s",tcp_port(tcb->conn.local.port));
		del_tcp(tcb);
		/* Clean up if server is being shut down */
		if(tcb == disc_tcb)
			disc_tcb = NULLTCB;
		else if(tcb == echo_tcb)
			echo_tcb = NULLTCB;
		break;
	}
}
/* Process remote exit/reset command */
static void
uremote(sock,cnt)
struct socket *sock;
int16 cnt;
{
	struct mbuf *bp;
	struct socket fsock;
	int i;
	char command,*cp;
	extern FILE *logfp;
	int32 addr;

	cp = ctime(&currtime);
	rip(cp);

	recv_udp(sock,&fsock,&bp);
	command = pullchar(&bp);
	switch(uchar(command)){
#ifdef  MSDOS   /* Only present on PCs running MSDOS */
	case SYS_RESET:
		i = chkrpass(&bp);
		log(Rem,"%s - Remote reset %s",
		 psocket((struct sockaddr *)&fsock),
		 i == 0 ? "PASSWORD FAIL" : "" );
		if(i != 0){
			iostop();
			sysreset();     /* No return */
		}
		break;
#endif
	case SYS_EXIT:
		i = chkrpass(&bp);
		if(logfp != NULLFILE){
			fprintf(logfp,"%s %s - Remote exit %s\n",
				cp,psocket(&fsock),
				i == 0 ? "PASSWORD FAIL" : "" );
			fflush(logfp);
		}
		if(i != 0){
			iostop();
			exit(0);
		}
		break;
	case KICK_ME:
		if(len_mbuf(bp) >= sizeof(int32))
			addr = pull32(&bp);
		else
			addr = fsock.address;
		kick(addr);
		/*** smtptick((void *)addr); ***/
		break;
	}
	free_p(bp);
}
/* Check remote password */
static int
chkrpass(bpp)
struct mbuf **bpp;
{
	char *lbuf;
	int16 len;
	int rval = 0;

	len = len_mbuf(*bpp);
	if(strlen(Rempass) == len) {
		lbuf = malloc(len);
		pullup(bpp,lbuf,len);
		if(strncmp(Rempass,lbuf,len) == 0)
			rval = 1;
		free(lbuf);
	}
	return rval;
}
