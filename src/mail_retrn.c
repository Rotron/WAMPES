/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/mail_retrn.c,v 1.6 1992-08-24 10:09:35 deyke Exp $ */

/* Mail Delivery Agent for returned Mails */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef LINUX
#include "linux.h"
#else
#include "hpux.h"
#endif
#include "mail.h"

/*---------------------------------------------------------------------------*/

void mail_return(jp)
struct mailjob *jp;
{

  FILE * fpi, *fpo;
  char  line[1024];
  int  i;

  fflush(stdout);
  if (fork()) return;
  rtprio_off();
  for (i = 0; i < _NFILE; i++) close(i);
  setpgrp();
  fopen("/dev/null", "r+");
  fopen("/dev/null", "r+");
  fopen("/dev/null", "r+");
  if (fpi = fopen(jp->dfile, "r")) {
    sprintf(line, "/usr/lib/sendmail -oi -oem -f '<>' %s", jp->from);
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
  unlink(jp->xfile);
  exit(0);
}

