/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/session.h,v 1.9 1993-05-17 13:45:16 deyke Exp $ */

#ifndef _SESSION_H
#define _SESSION_H

#include <stdio.h>

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

#ifndef _HARDWARE_H
#include "hardware.h"
#endif

#ifndef _TELNET_H
#include "telnet.h"
#endif

#ifndef _AX25_H
#include "ax25.h"
#endif

extern int Mode;
#define CMD_MODE        1       /* Command mode */
#define CONV_MODE       2       /* Converse mode */

/* Session control structure; only one entry is used at a time */
struct session {
	int type;
#define FREE    0
#define TELNET  1
#define FTP     2
#define AX25TNC 3
#define FINGER  4
#define NRSESSION 5
	char *name;     /* Name of remote host */
	union {
		struct ftp *ftp;
		struct telnet *telnet;
#ifdef  AX25
		struct ax25_cb *ax25;
#endif
		struct finger *finger;
#ifdef  NETROM
		struct circuit *netrom;
#endif
	} cb;
	void (*parse)(char *,int);
				/* Where to hand typed input when conversing */
	FILE *record;           /* Receive record file */
	char *rfile;            /* Record file name */
	FILE *upload;           /* Send file */
	char *ufile;            /* Upload file name */
};
#define NULLSESSION     (struct session *)0

extern unsigned Nsessions;              /* Maximum number of sessions */
extern struct session *Sessions;        /* Session descriptors themselves */
extern struct session *Current;         /* Always points to current session */

/* In session.c: */
void freesession(struct session *s);
struct session *newsession(void);
int dosession(int argc, char *argv[], void *p);
int go(int argc,char *argv[],void *p);
int doclose(int argc, char *argv[], void *p);
int doreset(int argc, char *argv[], void *p);
int dokick(int argc, char *argv[], void *p);
int dorecord(int argc, char *argv[], void *p);
int doupload(int argc, char *argv[], void *p);

/* In main.c: */
int cmdmode(void);

extern uint16 Lport;

#endif  /* _SESSION_H */
