/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/cmdparse.h,v 1.8 1993-02-23 21:34:05 deyke Exp $ */

#ifndef _CMDPARSE_H
#define _CMDPARSE_H

#define NARG            20      /* Max number of args to commands */

struct cmds {
	char *name;             /* Name of command */
	int (*func) __ARGS((int argc,char *argv[],void *p));
				/* Function to execute command */
	int stksize;            /* Size of stack if subprocess, 0 if synch */
	int  argcmin;           /* Minimum number of args */
	char *argc_errmsg;      /* Message to print if insufficient args */
};

#ifndef NULLCHAR
#define NULLCHAR        (char *)0
#endif

/* In cmdparse.c: */
int cmdparse __ARGS((struct cmds cmds[],char *line,void *p));
int subcmd __ARGS((struct cmds tab[],int argc,char *argv[],void *p));
int setbool __ARGS((int *var,char *label,int argc,char *argv[]));
int bit16cmd __ARGS((int16 *bits, int   mask, char *label, int argc, char *argv[]));
int setint __ARGS((int *var,char *label,int argc,char *argv[]));
int setlong __ARGS((int32 *var,char *label,int argc,char *argv[]));
int setshort __ARGS((unsigned short *var,char *label,int argc,char *argv[]));
int setuns __ARGS((unsigned *var,char *label,int argc,char *argv[]));
int setintrc __ARGS((int *var,char *label,int argc,char *argv[],int minval,int maxval));

#endif  /* _CMDPARSE_H */
