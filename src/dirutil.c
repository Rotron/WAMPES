/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/dirutil.c,v 1.14 1993-03-30 17:24:00 deyke Exp $ */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "global.h"
#include "hpux.h"
#include "dirutil.h"
#include "commands.h"

/* Create a directory listing in a temp file and return the resulting file
 * descriptor. If full == 1, give a full listing; else return just a list
 * of names.
 */
FILE *
dir(path,full)
char *path;
int full;
{
	int fd[2];

	if(pipe(fd))
		return NULLFILE;
	switch(dofork()) {
	case -1:
		close(fd[0]);
		close(fd[1]);
		return NULLFILE;
	case 0:
		close(fd[0]);
		dup2(fd[1],1);
		dup2(fd[1],2);
		close(fd[1]);
		execl("/bin/ls",
		      "ls",
		      full ? "-Al" : "-A",
		      path,
		      (char *) 0);
		exit(1);
	default:
		close(fd[1]);
		return fdopen(fd[0],"r");
	}
}

/* Create directory */
int
domkd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char buf[1024];

	if(mkdir(argv[1],0777) == -1){
		sprintf(buf,"Can't make %s",argv[1]);
		perror(buf);
	}
	return 0;
}
/* Remove directory */
int
dormd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char buf[1024];

	if(rmdir(argv[1]) == -1){
		sprintf(buf,"Can't remove %s",argv[1]);
		perror(buf);
	}
	return 0;
}
