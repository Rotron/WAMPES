#ifndef __lint
static const char rcsid[] = "@(#) $Header: /home/deyke/tmp/cvs/tcp/tools/check_seteugid.c,v 1.4 1996-08-11 18:17:09 deyke Exp $";
#endif

#include <stdio.h>
#include <unistd.h>

#include "seteugid.h"

/*---------------------------------------------------------------------------*/

static void print_ids(void)
{
  printf("ruid = %4d euid = %4d rgid = %4d egid = %4d\n",
	 (int) getuid(), (int) geteuid(), (int) getgid(), (int) getegid());
}

/*---------------------------------------------------------------------------*/

int main(void)
{
  print_ids();
  seteugid(1234, 4321);
  print_ids();
  seteugid(0, 0);
  print_ids();
  return 0;
}

