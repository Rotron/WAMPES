#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "icmp.h"
#include "netuser.h"
#include "tcp.h"
#include "telnet.h"
#include "session.h"

#define CTLZ    26

extern char nospace[];
extern char badhost[];
int refuse_echo = 0;
int unix_line_mode = 0;    /* if true turn <cr> to <nl> when in line mode */

#ifdef  DEBUG
char *t_options[] = {
	"Transmit Binary",
	"Echo",
	"",
	"Suppress Go Ahead",
	"",
	"Status",
	"Timing Mark"
};
#endif

/* Execute user telnet command */
int
dotelnet(argc,argv)
int argc;
char *argv[];
{
	void t_state(),rcv_char(),tn_tx();
	char *inet_ntoa();
	int32 resolve();
	int send_tel();
	int unix_send_tel();
	struct session *s;
	struct telnet *tn;
	struct tcb *tcb;
	struct socket lsocket,fsocket;

	lsocket.address = ip_addr;
	lsocket.port = lport++;
	if((fsocket.address = resolve(argv[1])) == 0){
		printf(badhost,argv[1]);
		return 1;
	}
	if(argc < 3)
		fsocket.port = TELNET_PORT;
	else
		fsocket.port = tcp_portnum(argv[2]);

	/* Allocate a session descriptor */
	if((s = newsession()) == NULLSESSION){
		printf("Too many sessions\n");
		return 1;
	}
	if((s->name = malloc((unsigned)strlen(argv[1])+1)) != NULLCHAR)
		strcpy(s->name,argv[1]);
	s->type = TELNET;
	if ((refuse_echo == 0) && (unix_line_mode != 0)) {
		s->parse = unix_send_tel;
	} else {
		s->parse = send_tel;
	}
	current = s;

	/* Create and initialize a Telnet protocol descriptor */
	if((tn = (struct telnet *)calloc(1,sizeof(struct telnet))) == NULLTN){
		printf(nospace);
		s->type = FREE;
		return 1;
	}
	tn->session = s;        /* Upward pointer */
	tn->state = TS_DATA;
	s->cb.telnet = tn;      /* Downward pointer */

	tcb = open_tcp(&lsocket,&fsocket,TCP_ACTIVE,0,
	 rcv_char,tn_tx,t_state,0,(char *)tn);

	tn->tcb = tcb;  /* Downward pointer */
	go();
	return 0;
}

/* Process typed characters */
int
unix_send_tel(buf,n)
char *buf;
int16 n;
{
	int i;

	for (i=0; (i<n) && (buf[i] != '\r'); i++)
		;
	if (buf[i] == '\r') {
		buf[i] = '\n';
		n = i+1;
	}
	send_tel(buf,n);
}
int
send_tel(buf,n)
char *buf;
int16 n;
{
	struct mbuf *bp,*qdata();
	if(current == NULLSESSION || current->cb.telnet == NULLTN
	 || current->cb.telnet->tcb == NULLTCB)
		return;
	/* If we're doing our own echoing and recording is enabled, record it */
	if(!current->cb.telnet->remote[TN_ECHO] && current->record != NULLFILE)
		fwrite(buf,1,n,current->record);
	bp = qdata(buf,n);
	send_tcp(current->cb.telnet->tcb,bp);
}

/* Process incoming TELNET characters */
int
tel_input(tn,bp)
register struct telnet *tn;
struct mbuf *bp;
{
	char c;
	void doopt(),dontopt(),willopt(),wontopt(),answer();
	FILE *record;
	char *memchr();

	/* Optimization for very common special case -- no special chars */
	if(tn->state == TS_DATA){
		while(bp != NULLBUF && memchr(bp->data,IAC,bp->cnt) == NULLCHAR){
			if((record = tn->session->record) != NULLFILE)
				fwrite(bp->data,1,bp->cnt,record);
			while(bp->cnt-- != 0)
				putchar(*bp->data++);
			bp = free_mbuf(bp);
		}
	}
	while(pullup(&bp,&c,1) == 1){
		switch(tn->state){
		case TS_DATA:
			if(uchar(c) == IAC){
				tn->state = TS_IAC;
			} else {
				if(!tn->remote[TN_TRANSMIT_BINARY])
					c &= 0x7f;
				putchar(c);
				if((record = tn->session->record) != NULLFILE)
					putc(c,record);
			}
			break;
		case TS_IAC:
			switch(uchar(c)){
			case WILL:
				tn->state = TS_WILL;
				break;
			case WONT:
				tn->state = TS_WONT;
				break;
			case DO:
				tn->state = TS_DO;
				break;
			case DONT:
				tn->state = TS_DONT;
				break;
			case IAC:
				putchar(c);
				tn->state = TS_DATA;
				break;
			default:
				tn->state = TS_DATA;
				break;
			}
			break;
		case TS_WILL:
			willopt(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_WONT:
			wontopt(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_DO:
			doopt(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_DONT:
			dontopt(tn,c);
			tn->state = TS_DATA;
			break;
		}
	}
}

/* Telnet receiver upcall routine */
void
rcv_char(tcb,cnt)
register struct tcb *tcb;
int16 cnt;
{
	struct mbuf *bp;
	struct telnet *tn;
	FILE *record;

	if((tn = (struct telnet *)tcb->user) == NULLTN){
		/* Unknown connection; ignore it */
		return;
	}
	/* Hold output if we're not the current session */
	if(mode != CONV_MODE || current == NULLSESSION
	 || current->type != TELNET || current->cb.telnet != tn)
		return;

	if(recv_tcp(tcb,&bp,cnt) > 0)
		tel_input(tn,bp);

	fflush(stdout);
	if((record = tn->session->record) != NULLFILE)
		fflush(record);
}
/* Handle transmit upcalls. Used only for file uploading */
void
tn_tx(tcb,cnt)
struct tcb *tcb;
int16 cnt;
{
	struct telnet *tn;
	struct session *s;
	struct mbuf *bp;
	int size;

	if((tn = (struct telnet *)tcb->user) == NULLTN
	 || (s = tn->session) == NULLSESSION
	 || s->upload == NULLFILE)
		return;
	if((bp = alloc_mbuf(cnt)) == NULLBUF)
		return;
	if((size = fread(bp->data,1,cnt,s->upload)) > 0){
		bp->cnt = (int16)size;
		send_tcp(tcb,bp);
	} else {
		free_p(bp);
	}
	if(size != cnt){
		/* Error or end-of-file */
		fclose(s->upload);
		s->upload = NULLFILE;
		free(s->ufile);
		s->ufile = NULLCHAR;
	}
}

/* State change upcall routine */
void
t_state(tcb,old,new)
register struct tcb *tcb;
char old,new;
{
	struct telnet *tn;
	char notify = 0;
	extern char *tcpstates[];
	extern char *reasons[];
	extern char *unreach[];
	extern char *exceed[];

	/* Can't add a check for unknown connection here, it would loop
	 * on a close upcall! We're just careful later on.
	 */
	tn = (struct telnet *)tcb->user;

	if(current != NULLSESSION && current->type == TELNET && current->cb.telnet == tn)
	{
		notify = 1;
		cooked();       /* prettify things... -- hyc */
	}

	switch(new){
	case CLOSE_WAIT:
		if(notify)
			printf("%s\n",tcpstates[new]);
		close_tcp(tcb);
		break;
	case CLOSED:    /* court adjourned */
		if(notify){
			printf("%s (%s",tcpstates[new],reasons[tcb->reason]);
			if(tcb->reason == NETWORK){
				switch(tcb->type){
				case DEST_UNREACH:
					printf(": %s unreachable",unreach[tcb->code]);
					break;
				case TIME_EXCEED:
					printf(": %s time exceeded",exceed[tcb->code]);
					break;
				}
			}
			printf(")\n");
			cmdmode();
		}
		del_tcp(tcb);
		if(tn != NULLTN)
			free_telnet(tn);
		break;
	default:
		if(notify)
			printf("%s\n",tcpstates[new]);
		break;
	}
	fflush(stdout);
}
/* Delete telnet structure */
static
free_telnet(tn)
struct telnet *tn;
{
	if(tn->session != NULLSESSION)
		freesession(tn->session);

	if(tn != NULLTN)
		free((char *)tn);
}

/* The guts of the actual Telnet protocol: negotiating options */
void
willopt(tn,opt)
struct telnet *tn;
char opt;
{
	int ack;
	void answer();

#ifdef  DEBUG
	printf("recv: will ");
	if(uchar(opt) <= NOPTIONS)
		printf("%s\n",t_options[opt]);
	else
		printf("%u\n",opt);
#endif

	switch(uchar(opt)){
	case TN_TRANSMIT_BINARY:
	case TN_ECHO:
	case TN_SUPPRESS_GA:
		if(tn->remote[uchar(opt)] == 1)
			return;         /* Already set, ignore to prevent loop */
		if(uchar(opt) == TN_ECHO){
			if(refuse_echo){
				/* User doesn't want to accept */
				ack = DONT;
				break;
			} else
				raw();          /* Put tty into raw mode */
		}
		tn->remote[uchar(opt)] = 1;
		ack = DO;
		break;
	default:
		ack = DONT;     /* We don't know what he's offering; refuse */
	}
	answer(tn,ack,opt);
}
void
wontopt(tn,opt)
struct telnet *tn;
char opt;
{
	void answer();

#ifdef  DEBUG
	printf("recv: wont ");
	if(uchar(opt) <= NOPTIONS)
		printf("%s\n",t_options[uchar(opt)]);
	else
		printf("%u\n",uchar(opt));
#endif
	if(uchar(opt) <= NOPTIONS){
		if(tn->remote[uchar(opt)] == 0)
			return;         /* Already clear, ignore to prevent loop */
		tn->remote[uchar(opt)] = 0;
		if(uchar(opt) == TN_ECHO)
			cooked();       /* Put tty into cooked mode */
	}
	answer(tn,DONT,opt);    /* Must always accept */
}
void
doopt(tn,opt)
struct telnet *tn;
char opt;
{
	void answer();
	int ack;

#ifdef  DEBUG
	printf("recv: do ");
	if(uchar(opt) <= NOPTIONS)
		printf("%s\n",t_options[uchar(opt)]);
	else
		printf("%u\n",uchar(opt));
#endif
	switch(uchar(opt)){
#ifdef  FUTURE  /* Use when local options are implemented */
		if(tn->local[uchar(opt)] == 1)
			return;         /* Already set, ignore to prevent loop */
		tn->local[uchar(opt)] = 1;
		ack = WILL;
		break;
#endif
	default:
		ack = WONT;     /* Don't know what it is */
	}
	answer(tn,ack,opt);
}
void
dontopt(tn,opt)
struct telnet *tn;
char opt;
{
	void answer();

#ifdef  DEBUG
	printf("recv: dont ");
	if(uchar(opt) <= NOPTIONS)
		printf("%s\n",t_options[uchar(opt)]);
	else
		printf("%u\n",uchar(opt));
#endif
	if(uchar(opt) <= NOPTIONS){
		if(tn->local[uchar(opt)] == 0){
			/* Already clear, ignore to prevent loop */
			return;
		}
		tn->local[uchar(opt)] = 0;
	}
	answer(tn,WONT,opt);
}
static
void
answer(tn,r1,r2)
struct telnet *tn;
int r1,r2;
{
	struct mbuf *bp,*qdata();
	char s[3];

#ifdef  DEBUG
	switch(r1){
	case WILL:
		printf("sent: will ");
		break;
	case WONT:
		printf("sent: wont ");
		break;
	case DO:
		printf("sent: do ");
		break;
	case DONT:
		printf("sent: dont ");
		break;
	}
	if(r2 <= 6)
		printf("%s\n",t_options[r2]);
	else
		printf("%u\n",r2);
#endif

	s[0] = IAC;
	s[1] = r1;
	s[2] = r2;
	bp = qdata(s,(int16)3);
	send_tcp(tn->tcb,bp);
}
