/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/mail.h,v 1.5 1990-10-22 11:38:17 deyke Exp $ */

#ifndef MAIL_INCLUDED
#define MAIL_INCLUDED

#include "global.h"

#define CONFFILE   "/tcp/mail.conf"
#define SPOOLDIR   "/usr/spool/uucp"
#define RETRYTIME  (60l*60)
#define RETURNTIME (60l*60*24*3)
#define MAXJOBS    10

struct mailers;

struct mailsys {
  char  *sysname;
  struct mailers *mailer;
  char  *protocol, *address;
  long  nexttime;
  struct mailjob *jobs;
  struct mailsys *next;
};

struct mailers {
  char  *name;
  void (*func) __ARGS((struct mailsys *sp));
};

struct mailjob {
  char  to[1024], from[1024], subject[1024];
  char  dfile[80], cfile[80], xfile[80];
  char  return_reason[1024];
  struct mailjob *next;
};

/* In mail_bbs.c: */
void mail_bbs __ARGS((struct mailsys *sp));

/* In mail_retrn.c: */
void mail_return __ARGS((struct mailjob *jp));

/* In mail_smtp.c: */
void mail_smtp __ARGS((struct mailsys *sp));

/* In mail_subr.c: */
char *get_user_from_path __ARGS((char *path));
char *get_host_from_path __ARGS((char *path));
void free_mailjobs __ARGS((struct mailsys *sp));

#endif  /* MAIL_INCLUDED */

