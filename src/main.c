/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/main.c,v 1.7 1990-09-11 13:45:56 deyke Exp $ */

/* Main network program - provides both client and server functions */
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef  __TURBOC__
#include <io.h>
#include <conio.h>
#endif
#include "global.h"
#ifdef  ANSIPROTO
#include <stdarg.h>
#endif
#include "files.h"
#include "mbuf.h"
#include "socket.h"
#include "iface.h"
#include "netuser.h"
#include "ftp.h"
#include "telnet.h"
#include "remote.h"
#include "session.h"
#include "cmdparse.h"
#include "ax25.h"
#include "kiss.h"
#include "enet.h"
#include "timer.h"
#include "netrom.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "commands.h"

extern long currtime;

extern struct cmds Cmds[],Startcmds[],Stopcmds[],Attab[];

#ifndef MSDOS                   /* PC uses F-10 key always */
static char Escape = 0x1d;      /* default escape character is ^] */
#endif

int debug;
int mode;
char Badhost[] = "Unknown host %s\n";
char *Hostname;
char Prompt[] = "%s> ";
char Nospace[] = "No space!!\n";        /* Generic malloc fail message */
static FILE *Logfp;
int32 resolve();
int16 lport = 1001;

main(argc,argv)
int argc;
char *argv[];
{
	static char inbuf[BUFSIZ];      /* keep it off the stack */
	int c;
	char *ttybuf,*fgets();
	int16 cnt;
	int ttydriv();
	int cmdparse();
	void check_time();
	FILE *fp;
	struct iface *ifp;

	debug = (argc >= 2);
	ioinit();
	netrom_initialize();
	remote_net_initialize();
#if     (defined(UNIX) || defined(AMIGA) || defined(MAC))
#else
	chktasker();
#endif
#ifdef  MSDOS
	printf("KA9Q Internet Protocol Package, v%s DS = %x\n",Version,
		getds());
#else
	printf("KA9Q Internet Protocol Package, v%s\n",Version);
#endif

	printf("Copyright 1988 by Phil Karn, KA9Q\n");
	sessions = (struct session *)calloc(Nsessions,sizeof(struct session));
	if(argc > 1){
		/* Read startup file named on command line */
		fp = fopen(argv[1],"r");
	} else {
		fp = fopen(Startup,"r");
	}
	if(fp != NULLFILE){
		while(fgets(inbuf,BUFSIZ,fp) != NULLCHAR){
			char intmp[BUFSIZ];
			strcpy(intmp,inbuf);
			if(cmdparse(Cmds,inbuf,NULL) != 0){
				tprintf("input line: %s",intmp);
			}
		}
		fclose(fp);
	}
	cmdmode();

	/* Main commutator loop */
	for(;;){
		/* Process any keyboard input */
		while((c = kbread()) != -1){
#if     (defined(MSDOS) || defined(ATARI_ST))
			/* c == -2 means the command escape key (F10) */
			if(c == -2){
				if(mode != CMD_MODE){
					printf("\n");
					cmdmode();
				}
				continue;
			}
#endif
#ifdef SYS5
			if(c == Escape && Escape != 0){
				ttydriv('\r', &ttybuf);
				mode = CONV_MODE;
				cmdmode();
				continue;
			}
#endif   /* SYS5 */
			if ((cnt = ttydriv(c, &ttybuf)) == 0)
				continue;
			switch(mode){
			case CMD_MODE:
				(void)cmdparse(Cmds,ttybuf,NULL);
				fflush(stdout);
				break;
			case CONV_MODE:
#ifdef  false
				if(ttybuf[0] == Escape && Escape != 0){
					printf("\n");
					cmdmode();
				} else
#endif  /* MSDOS */
					if(current->parse != NULLFP)
						(*current->parse)(ttybuf,cnt);

				break;
			}
			if(mode == CMD_MODE){
				printf(Prompt,Hostname);
				fflush(stdout);
			}
		}

		/* Service the interfaces */
		for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next){
			if(ifp->proc != NULLVFP)
				(*ifp->proc)(ifp);
		}

		/* Service the clock if it has ticked */
		check_time();

		if(Hopper)
			network();

#ifdef  MSDOS
		/* Tell DoubleDos to let the other task run for awhile.
		 * If DoubleDos isn't active, this is a no-op
		 */
		giveup();
#else
		/* Wait until interrupt, then do it all over again */
		eihalt();
#endif
	}
}
/* Standard commands called from main */

/* Enter command mode */
int
cmdmode()
{
	if(mode != CMD_MODE){
		mode = CMD_MODE;
		cooked();
		printf(Prompt,Hostname);
		fflush(stdout);
	}
	return 0;
}
doexit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(Logfp != NULLFILE)
		fclose(Logfp);
	iostop();
	exit(0);
}
int
dohostname(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2)
		tprintf("%s\n",Hostname);
	else {
		if(Hostname != NULLCHAR)
			free(Hostname);
		Hostname = strdup(argv[1]);
	}
	return 0;
}
int
dolog(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *strncpy();

	static char logname[256];
	if(argc < 2){
		if(Logfp)
			printf("Logging to %s\n",logname);
		else
			printf("Logging off\n");
		return 0;
	}
	if(Logfp){
		fclose(Logfp);
		Logfp = NULLFILE;
	}
	if(strcmp(argv[1],"stop") != 0){
		strncpy(logname,argv[1],sizeof(logname));
		Logfp = fopen(logname,"a+");
	}
	return 0;
}

/* Attach an interface
 * Syntax: attach <hw type> <I/O address> <vector> <mode> <label> <bufsize> [<sp
eed>]
 */
int
doattach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Attab,argc,argv,p);
}
/* Manipulate I/O device parameters */
doparam(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;

	if((ifp = if_lookup(argv[1])) == NULLIF){
		tprintf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	if(ifp->ioctl == NULLFP){
		tprintf("Not supported\n");
		return 1;
	}
	/* Pass rest of args to device-specific code */
	return (*ifp->ioctl)(ifp,argc-2,argv+2);
}
/* Log messages of the form
 * Tue Jan 31 00:00:00 1987 44.64.0.7:1003 open FTP
 */
/*VARARGS2*/
void
log(tcb,fmt,arg1,arg2,arg3,arg4)
struct tcb *tcb;
char *fmt;
int32 arg1,arg2,arg3,arg4;
{
	char *cp;

	if(Logfp == NULLFILE)
		return;
	cp = ctime(&currtime);
	rip(cp);
    if (tcb)
	fprintf(Logfp,"%s %s - ",cp,psocket(&tcb->conn.remote));
    else
	fprintf(Logfp,"%s - ",cp);
	fprintf(Logfp,fmt,arg1,arg2,arg3,arg4);
	fprintf(Logfp,"\n");
	fflush(Logfp);
#if     (defined(MSDOS) || defined(ATARI_ST))
	/* MS-DOS doesn't really flush files until they're closed */
	fd = fileno(Logfp);
	if((fd = dup(fd)) != -1)
		close(fd);
#endif
}

/* Display or set IP interface control flags */
int
domode(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;

	if((ifp = if_lookup(argv[1])) == NULLIF){
		tprintf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	if(argc < 3){
		tprintf("%s: %s\n",ifp->name,
		 (ifp->flags & CONNECT_MODE) ? "VC mode" : "Datagram mode");
		return 0;
	}
	switch(argv[2][0]){
	case 'v':
	case 'c':
	case 'V':
	case 'C':
		ifp->flags = CONNECT_MODE;
		break;
	case 'd':
	case 'D':
		ifp->flags = DATAGRAM_MODE;
		break;
	default:
		tprintf("Usage: %s [vc | datagram]\n",argv[0]);
		return 1;
	}
	return 0;
}

#if     ((!defined(MSDOS) && !defined(ATARI_ST)) || defined(PC9801))
int
doescape(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2)
		printf("0x%x\n",Escape);
	else
		Escape = *argv[1];
	return 0;
}
#endif  /* MSDOS */
/* Generate system command packet. Synopsis:
 * remote [-p port#] [-k key] [-a hostname] <hostname> reset|exit|kickme
 */
int
doremote(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct socket fsock,lsock;
	struct mbuf *bp;
	int c;
	int16 port,len;
	char *key = NULLCHAR;
	int klen;
	int32 addr = 0;
	char *cmd,*host;

	port = IPPORT_REMOTE;   /* Set default */
	optind = 1;             /* reinit getopt() */
	while((c = getopt(argc,argv,"a:p:k:s:")) != EOF){
		switch(c){
		case 'a':
			if (!(addr = resolve(optarg))) {
				printf("Host %s unknown\n",optarg);
				return 1;
			}
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'k':
			key = optarg;
			klen = strlen(key);
			break;
		case 's':
			Rempass = strdup(optarg);
			return 0;       /* Only set local password */
		}
	}
	if(optind > argc - 2){
		printf("Insufficient args\n");
		return 1;
	}
	host = argv[optind++];
	cmd = argv[optind];

	if (!(fsock.address = resolve(host))) {
		printf("Host %s unknown\n",host);
		return 1;
	}
	lsock.address = Ip_addr;
	lsock.port = fsock.port = port;

	len = 1;
	/* Did the user include a password or kickme target? */
	if(addr != 0)
		len += sizeof(int32);

	if(key != NULLCHAR)
		len += klen;

	bp = alloc_mbuf(len);
	bp->cnt = 1;

	switch(cmd[0]){
	case 'r':
		bp->data[0] = SYS_RESET;
		if(key != NULLCHAR) {
			strncpy(&bp->data[1],key,klen);
			bp->cnt += klen;
		}
		break;
	case 'e':
		bp->data[0] = SYS_EXIT;
		if(key != NULLCHAR) {
			strncpy(&bp->data[1],key,klen);
			bp->cnt += klen;
		}
		break;
	case 'k':
		bp->data[0] = KICK_ME;
		if(addr != 0) {
			put32(&bp->data[1],addr);
			bp->cnt += sizeof(int32);
		}
		break;
	default:
		printf("Unknown command %s\n",cmd);
		free_p(bp);
		return 1;
	}
	send_udp(&lsock,&fsock,0,0,bp,0,0,0);
	return 0;
}

/*---------------------------------------------------------------------------*/

int  dosource(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  FILE * fp;
  char  inbuf[BUFSIZ];

  if (!(fp = fopen(argv[1], "r"))) {
    printf("cannot open %s\n", argv[1]);
    return 1;
  }
  while (fgets(inbuf, BUFSIZ, fp))
    cmdparse(Cmds, inbuf, NULL);
  fclose(fp);
  mode = CMD_MODE;
  cooked();
  return 0;
}

/*---------------------------------------------------------------------------*/

#include <sys/rtprio.h>

int  dortprio(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  int  tmp;

  if (argc < 2) {
    tmp = rtprio(0, RTPRIO_NOCHG);
    if (tmp == RTPRIO_RTOFF)
      printf("Rtprio off\n");
    else
      printf("Rtprio %d\n", tmp);
  } else {
    tmp = atoi(argv[1]);
    if (tmp <= 0 || tmp > 127) tmp = RTPRIO_RTOFF;
    rtprio(0, tmp);
  }
  return 0;
}

