/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/mail_retrn.c,v 1.14 1993-06-21 21:46:37 deyke Exp $ */

/* Mail Delivery Agent for returned Mails */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#ifdef ULTRIX_RISC
#include <sys/signal.h>
#endif
#include <unistd.h>

#include "hpux.h"
#include "mail.h"

/*---------------------------------------------------------------------------*/

void mail_return(struct mailjob *jp)
{

  FILE *fpi, *fpo;
  char line[1024];
  int i;

  if (dofork()) return;
  for (i = 0; i < FD_SETSIZE; i++) close(i);
  setsid();
  fopen("/dev/null", "r+");
  fopen("/dev/null", "r+");
  fopen("/dev/null", "r+");
  if (fpi = fopen(jp->dfile, "r")) {
#ifdef __386BSD__
    sprintf(line, "/usr/sbin/sendmail -oi -oem -f '<>' %s", jp->from);
#else
    sprintf(line, "/usr/lib/sendmail -oi -oem -f '<>' %s", jp->from);
#endif
    if (!(fpo = popen(line, "w"))) exit(1);
    fprintf(fpo, "From: Mail Delivery Subsystem <MAILER-DAEMON>\n");
    fprintf(fpo, "To: %s\n", jp->from);
    fprintf(fpo, "Subject: Returned mail\n");
    putc('\n', fpo);
    fprintf(fpo, "   ----- Transcript of session follows -----\n");
    fprintf(fpo, "%s\n", jp->return_reason);
    putc('\n', fpo);
    fprintf(fpo, "   ----- Unsent message follows -----\n");
    fgets(line, sizeof(line), fpi);
    while (fgets(line, sizeof(line), fpi)) fputs(line, fpo);
    pclose(fpo);
    fclose(fpi);
  }
  unlink(jp->cfile);
  unlink(jp->dfile);
  if (*jp->xfile) unlink(jp->xfile);
  exit(0);
}

