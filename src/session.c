/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/session.c,v 1.3 1990-08-23 17:33:57 deyke Exp $ */

/* Session control */
#include <stdio.h>
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "ax25.h"
#include "iface.h"
#include "ftp.h"
#include "telnet.h"
#include "finger.h"
#include "session.h"
#include "cmdparse.h"

struct session *sessions;
struct session *current;
char notval[] = "Not a valid control block\n";
char badsess[] = "Invalid session\n";

/* Convert a character string containing a decimal session index number
 * into a pointer. If the arg is NULLCHAR, use the current default session.
 * If the index is out of range or unused, return NULLSESSION.
 */
static struct session *
sessptr(cp)
char *cp;
{
	register struct session *s;
	unsigned int i;

	if(cp == NULLCHAR){
		s = current;
	} else {
		if((i = atoi(cp)) >= nsessions)
			return NULLSESSION;
		s = &sessions[i];
	}
	if(s == NULLSESSION || s->type == FREE)
		return NULLSESSION;

	return s;
}

/* Select and display sessions */
dosession(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *s;
	extern char *tcpstates[];
	char *psocket();
	extern char *ax25states[];

	if(argc > 1){
		if((current = sessptr(argv[1])) != NULLSESSION)
			go(argc,argv,p);
		return 0;
	}
	printf(" #       &CB Type   Rcv-Q  State        Remote socket\n");
	for(s=sessions; s < &sessions[nsessions];s++){
		switch(s->type){
		case TELNET:
			printf("%c%-3d%8lx Telnet  %4d  %-13s%-s",
			 (current == s)? '*':' ',
			 (int)(s - sessions),
			 (long)s->cb.telnet->tcb,
			 s->cb.telnet->tcb->rcvcnt,
			 tcpstates[s->cb.telnet->tcb->state],
			 psocket(&s->cb.telnet->tcb->conn.remote));
			break;
		case FTP:
			printf("%c%-3d%8lx FTP     %4d  %-13s%-s",
			 (current == s)? '*':' ',
			 (int)(s - sessions),
			 (long)s->cb.ftp->control,
			 s->cb.ftp->control->rcvcnt,
			 tcpstates[s->cb.ftp->control->state],
			 psocket(&s->cb.ftp->control->conn.remote));
			break;
#ifdef  AX25
		case AX25TNC:
			print_ax25_session(s);
			break;
#endif
		case FINGER:
			printf("%c%-3d%8lx Finger  %4d  %-13s%-s",
			 (current == s)? '*':' ',
			 (int)(s - sessions),
			 (long)s->cb.finger->tcb,
			 s->cb.finger->tcb->rcvcnt,
			 tcpstates[s->cb.finger->tcb->state],
			 psocket(&s->cb.finger->tcb->conn.remote));
			break;
#ifdef NETROM
		case NRSESSION:
			print_netrom_session(s);
			break;
#endif
		default:
			continue;
		}
		if(s->rfile != NULLCHAR || s->ufile != NULLCHAR)
			printf("\t");
		if(s->rfile != NULLCHAR)
			printf("Record: %s ",s->rfile);
		if(s->ufile != NULLCHAR)
			printf("Upload: %s",s->ufile);
		printf("\n");
	}
	return 0;
}
/* Enter conversational mode with current session */
int
go(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	void rcv_char(),ftpccr(),ax_rx(),fingcli_rcv() ;

	if(current == NULLSESSION || current->type == FREE)
		return 0;
	mode = CONV_MODE;
	switch(current->type){
	case TELNET:
		if(current->cb.telnet->remote[TN_ECHO])
			raw();  /* Re-establish raw mode if it was set */
		rcv_char(current->cb.telnet->tcb,0); /* Get any pending input */
		break;
	case FTP:
		ftpccr(current->cb.ftp->control,0);
		break;
#ifdef  AX25
	case AX25TNC:
		axclient_recv_upcall(current->cb.ax25);
		break;
#endif
	case FINGER:
		fingcli_rcv(current->cb.finger->tcb,0) ;
		break ;
#ifdef  NETROM
	case NRSESSION:
		nrclient_recv_upcall(current->cb.netrom);
		break;
#endif
	}
	return 0;
}
doclose(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *s;

	if((s = sessptr(argc > 1 ? argv[1] : NULLCHAR)) == NULLSESSION){
		printf(badsess);
		return -1;
	}
	switch(s->type){
	case TELNET:
		close_tcp(s->cb.telnet->tcb);
		break;
	case FTP:
		close_tcp(s->cb.ftp->control);
		break;
#ifdef  AX25
	case AX25TNC:
		close_ax(s->cb.ax25);
		break;
#endif
	case FINGER:
		close_tcp(s->cb.finger->tcb);
		break;
#ifdef NETROM
	case NRSESSION:
		close_nr(s->cb.netrom);
		break;
#endif
	}
	return 0;
}
doreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	long htol();
	struct session *s;

	if((s = sessptr(argc > 1 ? argv[1] : NULLCHAR)) == NULLSESSION){
		printf(badsess);
		return -1;
	}
	switch(s->type){
	case TELNET:
		reset_tcp(s->cb.telnet->tcb);
		break;
	case FTP:
		if(s->cb.ftp->data != NULLTCB){
			reset_tcp(s->cb.ftp->data);
			s->cb.ftp->data = NULLTCB;
		}
		reset_tcp(s->cb.ftp->control);
		break;
#ifdef  AX25
	case AX25TNC:
		reset_ax(s->cb.ax25);
		break;
#endif
	case FINGER:
		reset_tcp(s->cb.finger->tcb);
		break;
#ifdef NETROM
	case NRSESSION:
		reset_nr(s->cb.netrom);
		break;
#endif
	}
	return 0;
}
int
dokick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	long htol();
	void tcp_timeout();
	struct session *s;

	if((s = sessptr(argc > 1 ? argv[1] : NULLCHAR)) == NULLSESSION){
		printf(badsess);
		return -1;
	}
	switch(s->type){
	case TELNET:
		if(kick_tcp(s->cb.telnet->tcb) == -1){
			printf(notval);
			return 1;
		}
		break;
	case FTP:
		if(kick_tcp(s->cb.ftp->control) == -1){

			printf(notval);
			return 1;
		}
		if(s->cb.ftp->data != NULLTCB)
			kick_tcp(s->cb.ftp->data);
		break;
#ifdef  AX25
	case AX25TNC:
		break;
#endif
	case FINGER:
		if(kick_tcp(s->cb.finger->tcb) == -1){
			printf(notval);
			return 1;
		}
		break;
#ifdef NETROM
	case NRSESSION:
		break;
#endif
	}
	return 0;
}
struct session *
newsession()
{
	register int i;

	for(i=0;i<nsessions;i++)
		if(sessions[i].type == FREE)
			return &sessions[i];
	return NULLSESSION;
}
freesession(s)
struct session *s;
{
	if(s == NULLSESSION)
		return;
	if(s->record != NULLFILE){
		fclose(s->record);
		s->record = NULLFILE;
	}
	if(s->rfile != NULLCHAR){
		free(s->rfile);
		s->rfile = NULLCHAR;
	}
	if(s->upload != NULLFILE){
		fclose(s->upload);
		s->upload = NULLFILE;
	}
	if(s->ufile != NULLCHAR){
		free(s->ufile);
		s->ufile = NULLCHAR;
	}
	if(s->name != NULLCHAR){
		free(s->name);
		s->name = NULLCHAR;
	}
	s->type = FREE;
	memset((char *) s, 0, sizeof(struct session));
	if(s == current)
		current = NULLSESSION;
}
/* Control session recording */
dorecord(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(current == NULLSESSION){
		printf("No current session\n");
		return 1;
	}
	if(argc > 1){
		if(current->rfile != NULLCHAR){
			fclose(current->record);
			free(current->rfile);
			current->record = NULLFILE;
			current->rfile = NULLCHAR;
		}
		/* Open new record file, unless file name is "off", which means
		 * disable recording
		 */
		if(strcmp(argv[1],"off") != 0
		 && (current->record = fopen(argv[1],"a")) != NULLFILE){
			current->rfile = malloc((unsigned)strlen(argv[1])+1);
			strcpy(current->rfile,argv[1]);
		}
	}
	if(current->rfile != NULLCHAR)
		printf("Recording into %s\n",current->rfile);
	else
		printf("Recording off\n");
	return 0;
}
/* Control file transmission */
doupload(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct tcb *tcb;
	struct axcb *axp;
	struct circuit *cb;

	if(current == NULLSESSION){
		printf("No current session\n");
		return 1;
	}
	if(argc > 1){
		switch(current->type){
		case FTP:
			printf("Uploading on FTP control channel not supported\n");
			return 1;
		case FINGER:
			printf("Uploading on FINGER session not supported\n");
			return 1;
		}
		/* Abort upload */
		if(current->upload != NULLFILE){
			fclose(current->upload);
			current->upload = NULLFILE;
		}
		if(current->ufile != NULLCHAR){
			free(current->ufile);
			current->ufile = NULLCHAR;
		}
		if(strcmp(argv[1],"stop") != 0){
			/* Open upload file */
			if((current->upload = fopen(argv[1],"r")) == NULLFILE){
				printf("Can't read %s\n",argv[1]);
				return 1;
			}
			current->ufile = malloc((unsigned)strlen(argv[1])+1);
			strcpy(current->ufile,argv[1]);
			/* All set, kick transmit upcall to get things rolling */
			switch(current->type){
#ifdef  AX25
			case AX25TNC:
				axp = current->cb.ax25;
				axclient_send_upcall(axp, space_ax(axp));
				break;
#endif
#ifdef NETROM
			case NRSESSION:
				cb = current->cb.netrom;
				nrclient_send_upcall(cb, space_nr(cb));
				break;
#endif
			case TELNET:
				tcb = current->cb.telnet->tcb;
				if(tcb->snd.wnd > tcb->sndcnt)
					(*tcb->t_upcall)(tcb,tcb->snd.wnd - tcb->sndcnt);
				break;
			}
		}
	}
	if(current->ufile != NULLCHAR)
		printf("Uploading %s\n",current->ufile);
	else
		printf("Uploading off\n");
	return 0;
}

