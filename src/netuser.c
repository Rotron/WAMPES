/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/netuser.c,v 1.4 1990-08-23 17:33:49 deyke Exp $ */

/* Miscellaneous format conversion subroutines */

#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "global.h"
#include "netuser.h"
#include "hpux.h"

int Net_error;

static struct hosttable {
  struct hosttable *next;
  int32 addr;
  char  name[1];
} *hosttable;

/*---------------------------------------------------------------------------*/

static void strlwc(to, from)
register char  *to, *from;
{
  while (*to++ = tolower(uchar(*from++))) ;
}

/*---------------------------------------------------------------------------*/

int32 aton(s)
register char  *s;
{

  register int  i;
  register int32 n;

  if (*s == '[') s++;
  for (n = 0, i = 24; i >= 0; i -= 8) {
    n |= atoi(s) << i;
    if (!(s = strchr(s, '.'))) break;
    s++;
  }
  return n;
}

/*---------------------------------------------------------------------------*/

static void add_to_hosttable(name, addr)
char  *name;
int32 addr;
{
  struct hosttable *hp;

  hp = (struct hosttable *) malloc(sizeof(struct hosttable ) + strlen(name));
  strlwc(hp->name, name);
  hp->addr = addr;
  hp->next = hosttable;
  hosttable = hp;
}

/*---------------------------------------------------------------------------*/

static void read_hosttable()
{

  FILE * fp;
  char  line[1024], addr[1024], name[1024];
  int32 n;
  register char  *p;
  static long  lastmtime;
  static long  nextchecktime;
  struct hosttable *hp;
  struct stat statbuf;

  if (nextchecktime > currtime) return;
  nextchecktime = currtime + 60;
  if (stat("/tcp/hosts", &statbuf)) return;
  if (lastmtime == statbuf.st_mtime || statbuf.st_mtime > currtime - 5) return;
  lastmtime = statbuf.st_mtime;
  while (hp = hosttable) {
    hosttable = hosttable->next;
    free((char *) hp);
  }
  if (!(fp = fopen("/tcp/hosts", "r"))) return;
  while (fgets(line, sizeof(line), fp)) {
    if (p = strchr(line, '#')) *p = '\0';
    if (sscanf(line, "%s %s", addr, name) == 2 && (n = aton(addr)))
      add_to_hosttable(name, n);
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

int32 resolve(name)
register char  *name;
{

  char  lname[1024];
  int32 n;
  register struct hosttable *p, *q;

  if (n = aton(name)) return n;
  strlwc(lname, name);
  read_hosttable();
  for (q = 0, p = hosttable; p; q = p, p = p->next)
    if (!strcmp(lname, p->name)) {
      if (q) {
	q->next = p->next;
	p->next = hosttable;
	hosttable = p;
      }
      return p->addr;
    }
  return 0;
}

/*---------------------------------------------------------------------------*/

char  *inet_ntoa(addr)
register int32 addr;
{

  char  buf[16];
  register struct hosttable *p, *q;

  if (!addr) return "*";
  read_hosttable();
  for (q = 0, p = hosttable; p; q = p, p = p->next)
    if (addr == p->addr) {
      if (q) {
	q->next = p->next;
	p->next = hosttable;
	hosttable = p;
      }
      return p->name;
    }
  sprintf(buf,
	  "%u.%u.%u.%u",
	  uchar(addr >> 24),
	  uchar(addr >> 16),
	  uchar(addr >>  8),
	  uchar(addr      ));
  add_to_hosttable(buf, addr);
  return hosttable->name;
}

/*---------------------------------------------------------------------------*/

char  *psocket(s)
struct socket *s;
{
  static char  buf[64];

  sprintf(buf, "%s:%s", inet_ntoa(s->address), tcp_port(s->port));
  return buf;
}

/*---------------------------------------------------------------------------*/

long  htol(s)
register char  *s;
{

  register int  c;
  register long  l = 0;

  while (isxdigit(c = (*s++ & 0x7f))) {
    if (c > '9') c -= 7;
    l = (l << 4) | (c & 0xf);
  }
  return l;
}

