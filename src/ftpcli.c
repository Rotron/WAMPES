/* FTP client (interactive user) code */
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "icmp.h"
#include "timer.h"
#include "tcp.h"
#include "ftp.h"
#include "session.h"
#include "cmdparse.h"

extern struct session *current;
extern char nospace[];
extern char badhost[];
static char notsess[] = "Not an FTP session!\n";
static char cantwrite[] = "Can't write %s\n";
static char cantread[] = "Can't read %s\n";

int donothing(),doftpcd(),dolist(),doget(),dols(),doput(),dotype(),doabort(),
	domkdir(),dormdir();
int doascii(),dobinary();

struct cmds ftpabort[] = {
	"",             donothing,      0,      NULLCHAR,               NULLCHAR,
	"abort",        doabort,        0,      NULLCHAR,               NULLCHAR,
	NULLCHAR,       NULLFP,         0,      "Only valid command is \"abort\"", NULLCHAR,
};

struct cmds ftpcmds[] = {
	"",             donothing,      0,      NULLCHAR,               NULLCHAR,
	"ascii",        doascii,        0,      NULLCHAR,               NULLCHAR,
	"binary",       dobinary,       0,      NULLCHAR,               NULLCHAR,
	"cd",           doftpcd,        2,      "cd <directory>",       NULLCHAR,
	"dir",          dolist,         0,      NULLCHAR,               NULLCHAR,
	"list",         dolist,         0,      NULLCHAR,               NULLCHAR,
	"get",          doget,          2,      "get remotefile <localfile>",   NULLCHAR,
	"ls",           dols,           0,      NULLCHAR,               NULLCHAR,
	"mkdir",        domkdir,        2,      "mkdir <directory>",    NULLCHAR,
	"nlst",         dols,           0,      NULLCHAR,               NULLCHAR,
	"rmdir",        dormdir,        2,      "rmdir <directory>",    NULLCHAR,
	"put",          doput,          2,      "put localfile <remotefile>",   NULLCHAR,
	"type",         dotype,         0,      NULLCHAR,               NULLCHAR,
	NULLCHAR,       NULLFP,         0,       NULLCHAR,              NULLCHAR,
};

/* Handle top-level FTP command */
doftp(argc,argv)
int argc;
char *argv[];
{
	int32 resolve();
	int ftpparse();
	char *inet_ntoa();
	void ftpccr(),ftpccs();
	struct session *s;
	struct ftp *ftp,*ftp_create();
	struct tcb *tcb;
	struct socket lsocket,fsocket;

	lsocket.address = ip_addr;
	lsocket.port = lport++;
	if((fsocket.address = resolve(argv[1])) == 0){
		printf(badhost,argv[1]);
		return 1;
	}
	if(argc < 3)
		fsocket.port = FTP_PORT;
	else
		fsocket.port = tcp_portnum(argv[2]);

	/* Allocate a session control block */
	if((s = newsession()) == NULLSESSION){
		printf("Too many sessions\n");
		return 1;
	}
	current = s;
	if((s->name = malloc((unsigned)strlen(argv[1])+1)) != NULLCHAR)
		strcpy(s->name,argv[1]);
	s->type = FTP;
	s->parse = ftpparse;

	/* Allocate an FTP control block */
	if((ftp = ftp_create(0)) == NULLFTP){
		s->type = FREE;
		printf(nospace);
		return 1;
	}
	ftp->state = COMMAND_STATE;
	s->cb.ftp = ftp;        /* Downward link */
	ftp->session = s;       /* Upward link */

	/* Now open the control connection */
	tcb = open_tcp(&lsocket,&fsocket,TCP_ACTIVE,
		0,ftpccr,NULLVFP,ftpccs,0,(char *)ftp);
	ftp->control = tcb;
	go();
	return 0;
}
/* Parse user FTP commands */
int
ftpparse(line,len)
char *line;
int16 len;
{
	struct mbuf *bp;

	if(current->cb.ftp->state != COMMAND_STATE){
		/* The only command allowed in data transfer state is ABORT */
		if(cmdparse(ftpabort,line) == -1){
			printf("Transfer in progress; only ABORT is acceptable\n");
		}
		fflush(stdout);
		return;
	}

	/* Save it now because cmdparse modifies the original */
	bp = qdata(line,len);

	if(cmdparse(ftpcmds,line) == -1){
		/* Send it direct */
		if(bp != NULLBUF)
			send_tcp(current->cb.ftp->control,bp);
		else
			printf(nospace);
	} else {
		free_p(bp);
	}
	fflush(stdout);
}
/* Handle null line to avoid trapping on first command in table */
static
int
donothing(argc,argv)
int argc;
char *argv[];
{
}
/* Translate 'cd' to 'cwd' for convenience */
static
int
doftpcd(argc,argv)
int argc;
char *argv[];
{
	register struct ftp *ftp;

	ftp = current->cb.ftp;
	return sndftpmsg(ftp,"CWD %s\r\n",argv[1]);
}
/* Translate 'mkdir' to 'xmkd' for convenience */
static
int
domkdir(argc,argv)
int argc;
char *argv[];
{
	register struct ftp *ftp;

	ftp = current->cb.ftp;
	return sndftpmsg(ftp,"XMKD %s\r\n",argv[1]);
}
/* Translate 'rmdir' to 'xrmd' for convenience */
static
int
dormdir(argc,argv)
int argc;
char *argv[];
{
	register struct ftp *ftp;

	ftp = current->cb.ftp;
	return sndftpmsg(ftp,"XRMD %s\r\n",argv[1]);
}
static
int
doascii(argc,argv)
int argc;
char *argv[];
{
	register struct ftp *ftp;

	ftp = current->cb.ftp;
	ftp->type = ASCII_TYPE;
	return sndftpmsg(ftp,"TYPE A\r\n");
}
static
int
dobinary(argc,argv)
int argc;
char *argv[];
{
	register struct ftp *ftp;

	ftp = current->cb.ftp;
	ftp->type = IMAGE_TYPE;
	return sndftpmsg(ftp,"TYPE I\r\n");
}
/* Handle "type" command from user */
static
int
dotype(argc,argv)
int argc;
char *argv[];
{
	register struct ftp *ftp;

	ftp = current->cb.ftp;
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
		sndftpmsg(ftp,"TYPE I\r\n");
		break;
	case 'A':
	case 'a':
		ftp->type = ASCII_TYPE;
		sndftpmsg(ftp,"TYPE A\r\n");
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
doget(argc,argv)
int argc;
char *argv[];
{
	void ftpdr(),ftpcds();
	char *remotename,*localname;
	register struct ftp *ftp;
	char *mode;

	ftp = current->cb.ftp;
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
		mode = binmode[WRITE_BINARY];
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
dolist(argc,argv)
int argc;
char *argv[];
{
	void ftpdr(),ftpcds();
	register struct ftp *ftp;

	ftp = current->cb.ftp;
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
dols(argc,argv)
int argc;
char *argv[];
{
	void ftpdr(),ftpcds();
	register struct ftp *ftp;

	ftp = current->cb.ftp;
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
doput(argc,argv)
int argc;
char *argv[];
{
	void ftpdt(),ftpcds();
	char *remotename,*localname;
	char *mode;
	struct ftp *ftp;

	if((ftp = current->cb.ftp) == NULLFTP){
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
		mode = binmode[READ_BINARY];
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
doabort(argc,argv)
int argc;
char *argv[];
{
	register struct ftp *ftp;

	ftp = current->cb.ftp;

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

	lsocket.address = ip_addr;
	lsocket.port = lport++;

	/* Compose and send PORT a,a,a,a,p,p message */

	if((bp = alloc_mbuf(35)) == NULLBUF){   /* 5 more than worst case */
		printf(nospace);
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
		recv,send,state,0,(char *)ftp);
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
	if(mode != CONV_MODE || current == NULLSESSION || current->cb.ftp != ftp)
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
	void ftp_delete();
	struct ftp *ftp;
	char notify = 0;
	extern char *tcpstates[];
	extern char *reasons[];
	extern char *unreach[];
	extern char *exceed[];

	/* Can't add a check for unknown connection here, it would loop
	 * on a close upcall! We're just careful later on.
	 */
	ftp = (struct ftp *)tcb->user;

	if(current != NULLSESSION && current->cb.ftp == ftp)
		notify = 1;

	switch(new){
	case CLOSE_WAIT:
		if(notify)
			printf("%s\n",tcpstates[new]);
		close_tcp(tcb);
		break;
	case CLOSED:    /* heh heh */
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
		if(ftp != NULLFTP)
			ftp_delete(ftp);
		break;
	default:
		if(notify)
			printf("%s\n",tcpstates[new]);
		break;
	}
	if(notify)
		fflush(stdout);
}
/* FTP Client Data channel State change upcall handler */
static
void
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
	case FINWAIT2:
	case TIME_WAIT:
		if(ftp->state == SENDING_STATE){
			/* We've received an ack of our FIN, so
			 * return to command mode
			 */
			ftp->state = COMMAND_STATE;
			if(current != NULLSESSION && current->cb.ftp == ftp){
				printf("Put complete, %lu bytes sent\n",
					tcb->snd.una - tcb->iss - 2);
				fflush(stdout);
			}
		}
		break;
	case CLOSE_WAIT:
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
			if(current != NULLSESSION && current->cb.ftp == ftp){
				printf("Get complete, %lu bytes received\n",
					tcb->rcv.nxt - tcb->irs - 2);
				fflush(stdout);
			}
		}
		break;
	case CLOSED:
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
		printf(nospace);
		return 1;
	}
	sprintf(bp->data,fmt,arg);
	bp->cnt = strlen(bp->data);
	send_tcp(ftp->control,bp);
	return 0;
}
