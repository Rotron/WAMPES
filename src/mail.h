/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/mail.h,v 1.14 1994-09-19 17:08:01 deyke Exp $ */

#ifndef _MAIL_H
#define _MAIL_H

#include "global.h"

#define CONFFILE   "/tcp/mail.conf"
#define RETRYTIME  (60L*60)
#define RETURNTIME (60L*60*24*3)
#define MAXJOBS    10

struct mailers;

enum e_mailer_state {
  MS_NEVER,                     /* Inactive states */
  MS_SUCCESS,
  MS_FAILURE,
  MS_TRYING,                    /* Active states */
  MS_TALKING
};

struct mailsys {
  char *sysname;
  const struct mailers *mailer;
  char *protocol, *address;
  enum e_mailer_state state;
  int nexttime;
  struct mailjob *jobs;
  struct mailsys *next;
};

struct mailers {
  char *name;
  void (*func)(struct mailsys *sp);
};

struct mailjob {
  char to[1024], from[1024], subject[1024];
  char dfile[80], cfile[80], xfile[80];
  char return_reason[1024];
  struct mailjob *next;
};

/* In mail_bbs.c: */
void mail_bbs(struct mailsys *sp);

/* In mail_retrn.c: */
void mail_return(struct mailjob *jp);

/* In mail_smtp.c: */
void mail_smtp(struct mailsys *sp);

/* In mail_subr.c: */
char *get_user_from_path(char *path);
char *get_host_from_path(char *path);
void mailer_failed(struct mailsys *sp);

#endif  /* _MAIL_H */
