/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/ftpcli.c,v 1.3 1990-10-12 19:25:40 deyke Exp $ */

/* FTP client (interactive user) code */
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "icmp.h"
#include "timer.h"
#include "tcp.h"
#include "socket.h"
#include "ftp.h"
#include "session.h"
#include "cmdparse.h"

void ftpdr(),ftpdt(),ftpccr();
int doabort();

static char notsess[] = "Not an FTP session!\n";
static char cantwrite[] = "Can't write %s\n";
static char cantread[] = "Can't read %s\n";

static int donothing __ARGS((int argc, char *argv [], void *p));
static int doftpcd __ARGS((int argc, char *argv [], void *p));
static int domkdir __ARGS((int argc, char *argv [], void *p));
static int dormdir __ARGS((int argc, char *argv [], void *p));
static int doascii __ARGS((int argc, char *argv [], void *p));
static int dobinary __ARGS((int argc, char *argv [], void *p));
static int dotype __ARGS((int argc, char *argv [], void *p));
static doget __ARGS((int argc, char *argv [], void *p));
static dolist __ARGS((int argc, char *argv [], void *p));
static dols __ARGS((int argc, char *argv [], void *p));
static doput __ARGS((int argc, char *argv [], void *p));
static ftpsetup __ARGS((struct ftp *ftp, void (*recv )(), void (*send )(), void (*state )()));
static void ftpccs __ARGS((struct tcb *tcb, int old, int new));
static void ftpcds __ARGS((struct tcb *tcb, int old, int new));
static int sndftpmsg __ARGS((struct ftp *ftp, char *fmt, char *arg));

struct cmds ftpabort[] = {
	"",             donothing,      0, 0,      NULLCHAR,
	"abort",        doabort,        0, 0,      NULLCHAR,
	NULLCHAR,       NULLFP,         0, 0,      "Only valid command is \"abort\""
};

struct cmds ftpcmds[] = {
	"",             donothing,      0, 0,      NULLCHAR,
	"ascii",        doascii,        0, 0,      NULLCHAR,
	"binary",       dobinary,       0, 0,      NULLCHAR,
	"cd",           doftpcd,        0, 2,      "cd <directory>",
	"dir",          dolist,         0, 0,      NULLCHAR,
	"list",         dolist,         0, 0,      NULLCHAR,
	"get",          doget,          0, 2,      "get remotefile <localfile>",
	"ls",           dols,           0, 0,      NULLCHAR,
	"mkdir",        domkdir,        0, 2,      "mkdir <directory>",
	"nlst",         dols,           0, 0,      NULLCHAR,
	"rmdir",        dormdir,        0, 2,      "rmdir <directory>",
	"put",          doput,          0, 2,      "put localfile <remotefile>",
	"type",         dotype,         0, 0,      NULLCHAR,
	NULLCHAR,       NULLFP,         0, 0,      NULLCHAR
};

/* Handle top-level FTP command */
doftp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int32 resolve();
	int ftpparse();
	struct session *s;
	struct ftp *ftp,*ftp_create();
	struct tcb *tcb;
	struct socket lsocket,fsocket;

	lsocket.address = Ip_addr;
	lsocket.port = Lport++;
	if((fsocket.address = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	}
	if(argc < 3)
		fsocket.port = IPPORT_FTP;
	else
		fsocket.port = tcp_port_number(argv[2]);

	/* Allocate a session control block */
	if((s = newsession()) == NULLSESSION){
		printf("Too many sessions\n");
		return 1;
	}
	Current = s;
	if((s->name = malloc((unsigned)strlen(argv[1])+1)) != NULLCHAR)
		strcpy(s->name,argv[1]);
	s->type = FTP;
	s->parse = ftpparse;

	/* Allocate an FTP control block */
	if((ftp = ftp_create(0)) == NULLFTP){
		s->type = FREE;
		printf(Nospace);
		return 1;
	}
	ftp->state = COMMAND_STATE;
	s->cb.ftp = ftp;        /* Downward link */
	ftp->session = s;       /* Upward link */

	/* Now open the control connection */
	tcb = open_tcp(&lsocket,&fsocket,TCP_ACTIVE,
		0,ftpccr,NULLVFP,ftpccs,0,(int)ftp);
	ftp->control = tcb;
	go(argc, argv, p);
	return 0;
}
/* Parse user FTP commands */
int
ftpparse(line,len)
char *line;
int16 len;
{
	struct mbuf *bp;

	if(Current->cb.ftp->state != COMMAND_STATE){
		/* The only command allowed in data transfer state is ABORT */
		if(cmdparse(ftpabort,line,NULL) == -1){
			printf("Transfer in progress; only ABORT is acceptable\n");
		}
		fflush(stdout);
		return;
	}

	/* Save it now because cmdparse modifies the original */
	bp = qdata(line,len);

	if(cmdparse(ftpcmds,line,NULL) == -1){
		/* Send it direct */
		if(bp != NULLBUF)
			send_tcp(Current->cb.ftp->control,bp);
		else
			printf(Nospace);
	} else {
		free_p(bp);
	}
	fflush(stdout);
}
/* Handle null line to avoid trapping on first command in table */
static
int
donothing(argc,argv,p)
int argc;
char *argv[];
void *p;
{
}
/* Translate 'cd' to 'cwd' for convenience */
static
int
doftpcd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	return sndftpmsg(ftp,"CWD %s\r\n",argv[1]);
}
/* Translate 'mkdir' to 'xmkd' for convenience */
static
int
domkdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	return sndftpmsg(ftp,"XMKD %s\r\n",argv[1]);
}
/* Translate 'rmdir' to 'xrmd' for convenience */
static
int
dormdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	return sndftpmsg(ftp,"XRMD %s\r\n",argv[1]);
}
static
int
doascii(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	ftp->type = ASCII_TYPE;
	return sndftpmsg(ftp,"TYPE A\r\n","");
}
static
int
dobinary(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	ftp->type = IMAGE_TYPE;
	return sndftpmsg(ftp,"TYPE I\r\n","");
}
/* Handle "type" command from user */
static
int
dotype(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(argc < 2){
		switch(ftp->type){
		case IMAGE_TYPE:
			printf("Using binary mode to transfer files.\n");
			break;
		case ASCII_TYPE:
			printf("Using ascii mode to transfer files.\n");
			break;
		}
		return 0;
	}
	switch(*argv[1]){
	case 'I':
	case 'i':
	case 'B':
	case 'b':
		ftp->type = IMAGE_TYPE;
		sndftpmsg(ftp,"TYPE I\r\n","");
		break;
	case 'A':
	case 'a':
		ftp->type = ASCII_TYPE;
		sndftpmsg(ftp,"TYPE A\r\n","");
		break;
	case 'L':
	case 'l':
		ftp->type = IMAGE_TYPE;
		sndftpmsg(ftp,"TYPE L %s\r\n",argv[2]);
		break;
	default:
		printf("Invalid type %s\n",argv[1]);
		return 1;
	}
	return 0;
}
/* Start receive transfer. Syntax: get <remote name> [<local name>] */
static
doget(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename,*localname;
	register struct ftp *ftp;
	char *mode;

	ftp = Current->cb.ftp;
	if(ftp == NULLFTP){
		printf(notsess);
		return 1;
	}
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	remotename = argv[1];
	if(argc < 3)
		localname = remotename;
	else
		localname = argv[2];

	if(ftp->type == IMAGE_TYPE)
		mode = WRITE_BINARY;
	else
		mode = "w";

	if(!strcmp(localname, "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(localname,mode)) == NULLFILE){
		printf(cantwrite,localname);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULLVFP,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"RETR %s\r\n",remotename);
}
/* List remote directory. Syntax: dir <remote directory/file> [<local name>] */
static
dolist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp == NULLFTP){
		printf(notsess);
		return 1;
	}
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	if(argc < 3 || !strcmp(argv[2], "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(argv[2],"w")) == NULLFILE){
		printf(cantwrite,argv[2]);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULLVFP,ftpcds);
	/* Generate the command to start the transfer
	 * It's done this way to avoid confusing the 4.2 FTP server
	 * if there's no argument
	 */
	if(argc > 1)
		return sndftpmsg(ftp,"LIST %s\r\n",argv[1]);
	else
		return sndftpmsg(ftp,"LIST\r\n","");
}
/* Abbreviated (name only) list of remote directory.
 * Syntax: ls <remote directory/file> [<local name>]
 */
static
dols(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp == NULLFTP){
		printf(notsess);
		return 1;
	}
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	if(argc < 3 || !strcmp(argv[2], "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(argv[2],"w")) == NULLFILE){
		printf(cantwrite,argv[2]);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULLVFP,ftpcds);
	/* Generate the command to start the transfer */
	if(argc > 1)
		return sndftpmsg(ftp,"NLST %s\r\n",argv[1]);
	else
		return sndftpmsg(ftp,"NLST\r\n","");
}
/* Start transmit. Syntax: put <local name> [<remote name>] */
static
doput(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename,*localname;
	char *mode;
	struct ftp *ftp;

	if((ftp = Current->cb.ftp) == NULLFTP){
		printf(notsess);
		return 1;
	}
	localname = argv[1];
	if(argc < 3)
		remotename = localname;
	else
		remotename = argv[2];

	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);

	if(ftp->type == IMAGE_TYPE)
		mode = READ_BINARY;
	else
		mode = "r";

	if((ftp->fp = fopen(localname,mode)) == NULLFILE){
		printf(cantread,localname);
		return 1;
	}
	ftp->state = SENDING_STATE;
	ftpsetup(ftp,NULLVFP,ftpdt,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"STOR %s\r\n",remotename);
}
/* Abort a GET or PUT operation in progress. Note: this will leave
 * the partial file on the local or remote system
 */
doabort(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;

	/* Close the local file */
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	switch(ftp->state){
	case SENDING_STATE:
		/* Send a premature EOF.
		 * Unfortunately we can't just reset the connection
		 * since the remote side might end up waiting forever
		 * for us to send something.
		 */
		close_tcp(ftp->data);
		printf("Put aborted\n");
		break;
	case RECEIVING_STATE:
		/* Just exterminate the data channel TCB; this will
		 * generate a RST on the next data packet which will
		 * abort the sender
		 */
		del_tcp(ftp->data);
		ftp->data = NULLTCB;
		printf("Get aborted\n");
		break;
	}
	ftp->state = COMMAND_STATE;
	fflush(stdout);
}
/* create data port, and send PORT message */
static
ftpsetup(ftp,recv,send,state)
struct ftp *ftp;
void (*send)();
void (*recv)();
void (*state)();
{
	struct socket lsocket;
	struct mbuf *bp;

	lsocket.address = Ip_addr;
	lsocket.port = Lport++;

	/* Compose and send PORT a,a,a,a,p,p message */

	if((bp = alloc_mbuf(35)) == NULLBUF){   /* 5 more than worst case */
		printf(Nospace);
		return;
	}
	/* I know, this looks gross, but it works! */
	sprintf(bp->data,"PORT %u,%u,%u,%u,%u,%u\r\n",
		hibyte(hiword(lsocket.address)),
		lobyte(hiword(lsocket.address)),
		hibyte(loword(lsocket.address)),
		lobyte(loword(lsocket.address)),
		hibyte(lsocket.port),
		lobyte(lsocket.port));
	bp->cnt = strlen(bp->data);
	send_tcp(ftp->control,bp);

	/* Post a listen on the data connection */
	ftp->data = open_tcp(&lsocket,NULLSOCK,TCP_PASSIVE,0,
		recv,send,state,0,(int)ftp);
}
/* FTP Client Control channel Receiver upcall routine */
void
ftpccr(tcb,cnt)
register struct tcb *tcb;
int16 cnt;
{
	struct mbuf *bp;
	struct ftp *ftp;

	if((ftp = (struct ftp *)tcb->user) == NULLFTP){
		/* Unknown connection; kill it */
		close_tcp(tcb);
		return;
	}
	/* Hold output if we're not the current session */
	if(mode != CONV_MODE || Current == NULLSESSION || Current->cb.ftp != ftp)
		return;

	if(recv_tcp(tcb,&bp,cnt) > 0){
		while(bp != NULLBUF){
			fwrite(bp->data,1,(unsigned)bp->cnt,stdout);
			bp = free_mbuf(bp);
		}
		fflush(stdout);
	}
}

/* FTP Client Control channel State change upcall routine */
static
void
ftpccs(tcb,old,new)
register struct tcb *tcb;
char old,new;
{
	struct ftp *ftp;
	char notify = 0;

	/* Can't add a check for unknown connection here, it would loop
	 * on a close upcall! We're just careful later on.
	 */
	ftp = (struct ftp *)tcb->user;

	if(Current != NULLSESSION && Current->cb.ftp == ftp)
		notify = 1;

	switch(new){
	case TCP_CLOSE_WAIT:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		close_tcp(tcb);
		break;
	case TCP_CLOSED:    /* heh heh */
		if(notify){
			printf("%s (%s",Tcpstates[new],Tcpreasons[tcb->reason]);
			if(tcb->reason == NETWORK){
				switch(tcb->type){
				case ICMP_DEST_UNREACH:
					printf(": %s unreachable",Unreach[tcb->code]);
					break;
				case ICMP_TIME_EXCEED:
					printf(": %s time exceeded",Exceed[tcb->code]);
					break;
				}
			}
			printf(")\n");
			cmdmode();
		}
		del_tcp(tcb);
		if(ftp != NULLFTP)
			ftp_delete(ftp);
		break;
	default:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		break;
	}
	if(notify)
		fflush(stdout);
}
/* FTP Client Data channel State change upcall handler */
static void
ftpcds(tcb,old,new)
struct tcb *tcb;
char old,new;
{
	struct ftp *ftp;

	if((ftp = (struct ftp *)tcb->user) == NULLFTP){
		/* Unknown connection, kill it */
		close_tcp(tcb);
		return;
	}
	switch(new){
	case TCP_FINWAIT2:
	case TCP_TIME_WAIT:
		if(ftp->state == SENDING_STATE){
			/* We've received an ack of our FIN, so
			 * return to command mode
			 */
			ftp->state = COMMAND_STATE;
			if(Current != NULLSESSION && Current->cb.ftp == ftp){
				printf("Put complete, %lu bytes sent\n",
					tcb->snd.una - tcb->iss - 2);
				fflush(stdout);
			}
		}
		break;
	case TCP_CLOSE_WAIT:
		close_tcp(tcb);
		if(ftp->state == RECEIVING_STATE){
			/* End of file received on incoming file */
#ifdef  CPM
			if(ftp->type == ASCII_TYPE)
				putc(CTLZ,ftp->fp);
#endif
			if(ftp->fp != stdout)
				fclose(ftp->fp);
			ftp->fp = NULLFILE;
			ftp->state = COMMAND_STATE;
			if(Current != NULLSESSION && Current->cb.ftp == ftp){
				printf("Get complete, %lu bytes received\n",
					tcb->rcv.nxt - tcb->irs - 2);
				fflush(stdout);
			}
		}
		break;
	case TCP_CLOSED:
		ftp->data = NULLTCB;
		del_tcp(tcb);
		break;
	}
}
/* Send a message on the control channel */
/*VARARGS*/
static
int
sndftpmsg(ftp,fmt,arg)
struct ftp *ftp;
char *fmt;
char *arg;
{
	struct mbuf *bp;
	int16 len;

	len = strlen(fmt) + strlen(arg) + 10;   /* fudge factor */
	if((bp = alloc_mbuf(len)) == NULLBUF){
		printf(Nospace);
		return 1;
	}
	sprintf(bp->data,fmt,arg);
	bp->cnt = strlen(bp->data);
	send_tcp(ftp->control,bp);
	return 0;
}
