static char  rcsid[] = "@(#) $Header: /home/deyke/tmp/cvs/tcp/convers/conversd.c,v 1.3 1988-08-30 22:19:42 dk5sg Exp $";

#include <sys/types.h>

#include <ctype.h>
#include <memory.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

extern char  *calloc();
extern char  *malloc();
extern long  time();
extern void exit();
extern void free();

#define PORT 9876

struct mbuf {
  struct mbuf *next;
  char  *data;
};

struct connection {
  struct connection *next;
  int  fd;
  long  time;
  int  channel;
  int  closed;
  char  user[80];
  char  ibuf[2048];
  int  icnt;
  struct mbuf *obuf;
};

static struct connection *connections;

/*---------------------------------------------------------------------------*/

static void appendstring(bpp, string)
struct mbuf **bpp;
char  *string;
{
  register struct mbuf *bp, *p;

  if (!*string) return;

  bp = (struct mbuf *) malloc(sizeof(struct mbuf ) + strlen(string) + 1);
  bp->next = 0;
  bp->data = strcpy((char *) (bp + 1), string);

  if (*bpp) {
    for (p = *bpp; p->next; p = p->next) ;
    p->next = bp;
  } else
    *bpp = bp;
}

/*---------------------------------------------------------------------------*/

static int  pullchar(bpp)
struct mbuf **bpp;
{

  int  c;
  register struct mbuf *bp, *p;

  bp = *bpp;
  c = (*bp->data++) & 0xff;
  while (bp && !*bp->data) {
    p = bp;
    bp = bp->next;
    free(p);
  }
  *bpp = bp;
  return c;
}

/*---------------------------------------------------------------------------*/

static void freequeue(bpp)
struct mbuf **bpp;
{
  register struct mbuf *bp, *p;

  bp = *bpp;
  while (bp) {
    p = bp;
    bp = bp->next;
    free(p);
  }
  *bpp = 0;
}

/*---------------------------------------------------------------------------*/

static void free_closed_connections()
{
  register struct connection *cp, *p;

  for (p = 0, cp = connections; cp; ) {
    if (!cp->closed) {
      p = cp;
      cp = cp->next;
    } else if (p) {
      p->next = cp->next;
      close(cp->fd);
      freequeue(&cp->obuf);
      free((char *) cp);
      cp = p->next;
    } else {
      connections = cp->next;
      close(cp->fd);
      freequeue(&cp->obuf);
      free((char *) cp);
      cp = connections;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void sendchannel(cp, string)
struct connection *cp;
char  *string;
{
  register struct connection *p;

  for (p = connections; p; p = p->next)
    if (p->channel == cp->channel && p != cp) appendstring(&p->obuf, string);
}

/*---------------------------------------------------------------------------*/

static char  *getarg(iptr)
char  *iptr;
{

  char  *arg;
  int  c;
  static char  *ip;

  if (iptr) ip = iptr;
  while (*ip == ' ' || *ip == '\t') ip++;
  arg = ip;
  while (*ip && *ip != ' ' && *ip != '\t') {
    c = tolower(*ip & 0xff);
    *ip++ = c;
  }
  if (*ip) *ip++ = '\0';
  return arg;
}

/*---------------------------------------------------------------------------*/

static void doconnect(flisten)
int  flisten;
{

  int  addrlen;
  int  fd;
  struct connection *cp;
  struct sockaddr_in addr_in;

  addrlen = sizeof(addr_in);
  if ((fd = accept(flisten, &addr_in, &addrlen)) < 0) return;
  cp = (struct connection *) calloc(1, sizeof(struct connection ));
  cp->next = connections;
  connections = cp;
  cp->fd = fd;
  cp->time = time(0l);
  appendstring(&cp->obuf, "conversd $Revision: 1.3 $  Type /HELP for help.\n");
}

/*---------------------------------------------------------------------------*/

static void bye_command(cp)
struct connection *cp;
{
  char  buffer[2048];

  cp->closed = 1;
  sprintf(buffer, "*** %s signed off.\n", cp->user);
  sendchannel(cp, buffer);
}

/*---------------------------------------------------------------------------*/

static void channel_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  int  newchannel;

  newchannel = atoi(getarg(0));
  if (cp->channel == newchannel) return;
  sprintf(buffer, "*** %s signed off.\n", cp->user);
  sendchannel(cp, buffer);
  cp->channel = newchannel;
  sprintf(buffer, "*** %s signed on.\n", cp->user);
  sendchannel(cp, buffer);
}

/*---------------------------------------------------------------------------*/

static void help_command(cp)
struct connection *cp;
{
  appendstring(&cp->obuf, "Commands may be abbreviated. Commands are:\n");

  appendstring(&cp->obuf, "/?           Print help information\n");
  appendstring(&cp->obuf, "/BYE         Terminate the convers session\n");
  appendstring(&cp->obuf, "/CHANNEL n   Switch to channel n\n");
  appendstring(&cp->obuf, "/EXIT        Terminate the convers session\n");
  appendstring(&cp->obuf, "/HELP        Print help information\n");
  appendstring(&cp->obuf, "/QUIT        Terminate the convers session\n");
  appendstring(&cp->obuf, "/WHO         Print all users and their channel numbers\n");

  appendstring(&cp->obuf, "***\n");
}

/*---------------------------------------------------------------------------*/

static void name_command(cp)
struct connection *cp;
{
  char  buffer[2048];

  if (*cp->user) return;
  strcpy(cp->user, getarg(0));
  sprintf(buffer, "*** %s signed on.\n", cp->user);
  sendchannel(cp, buffer);
}

/*---------------------------------------------------------------------------*/

static void who_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  struct connection *p;
  struct tm *tm;

  appendstring(&cp->obuf, "Channel  Time   User\n");
  for (p = connections; p; p = p->next) {
    tm = localtime(&p->time);
    sprintf(buffer, "%7d  %2d:%02d  %s\n", p->channel, tm->tm_hour, tm->tm_min, p->user);
    appendstring(&cp->obuf, buffer);
  }
  appendstring(&cp->obuf, "***\n");
}

/*---------------------------------------------------------------------------*/

static char  *formatline(prefix, text)
char  *prefix, *text;
{

#define PREFIXLEN 10
#define LINELEN   79

  register char  *f, *t, *x;
  register int  l, lw;

  static char  buf[2048];

  for (f = prefix, t = buf, l = 0; *f; *t++ = *f++, l++) ;
  f = text;

  for (; ; ) {
    while (isspace(*f & 0xff)) f++;
    if (!*f) {
      *t++ = '\n';
      *t = '\0';
      return buf;
    }
    for (lw = 0, x = f; *x && !isspace(*x & 0xff); x++, lw++) ;
    if (l > PREFIXLEN && l + 1 + lw > LINELEN) {
      *t++ = '\n';
      l = 0;
    }
    do {
      *t++ = ' ';
      l++;
    } while (l < PREFIXLEN);
    while (lw--) {
      *t++ = *f++;
      l++;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void process_input(cp)
struct connection *cp;
{

  static struct cmdtable {
    char  *name;
    void (*fnc)();
  } cmdtable[] = {

    "?",       help_command,
    "bye",     bye_command,
    "channel", channel_command,
    "exit",    bye_command,
    "help",    help_command,
    "name",    name_command,
    "quit",    bye_command,
    "who",     who_command,

    0, 0
  };

  char  *arg;
  char  buffer[2048];
  int  arglen;
  struct cmdtable *cmdp;

  if (*cp->ibuf == '/') {
    arglen = strlen(arg = getarg(cp->ibuf + 1));
    for (cmdp = cmdtable; cmdp->name; cmdp++)
      if (!strncmp(cmdp->name, arg, arglen)) {
	(*cmdp->fnc)(cp);
	return;
      }
    sprintf(buffer, "*** Unknown command '/%s'. Type /HELP for help.\n", arg);
    appendstring(&cp->obuf, buffer);
    return;
  }
  sprintf(buffer, "<%s>:", cp->user);
  sendchannel(cp, formatline(buffer, cp->ibuf));
}

/*---------------------------------------------------------------------------*/

main()
{

  char  buffer[2048];
  char  c;
  int  flisten;
  int  i;
  int  nfd;
  int  rmask, wmask;
  int  size;
  struct connection *cp;
  struct sockaddr_in addr_in;

  for (i = 0; i < _NFILE; i++) close(i);
  chdir("/");
  setpgrp();
  putenv("TZ=MEZ-1MESZ");

  if ((flisten = socket(AF_INET, SOCK_STREAM, 0)) < 0) exit(1);
  memset((char *) & addr_in, 0, sizeof(addr_in ));
  addr_in.sin_family = AF_INET;
  addr_in.sin_addr.s_addr = INADDR_ANY;
  addr_in.sin_port = PORT;
  setsockopt(flisten, SOL_SOCKET, SO_REUSEADDR, (char *) 0, 0);
  if (bind(flisten, &addr_in, sizeof(addr_in ))) exit(1);
  if (listen(flisten, 20)) exit(1);

  for (; ; ) {

    free_closed_connections();

    nfd = flisten + 1;
    rmask = (1 << flisten);
    wmask = 0;
    for (cp = connections; cp; cp = cp->next) {
      if (nfd <= cp->fd) nfd = cp->fd + 1;
      rmask |= (1 << cp->fd);
      if (cp->obuf) wmask |= (1 << cp->fd);
    }
    select(nfd, &rmask, &wmask, (int *) 0, (struct timeval *) 0);

    if (rmask & (1 << flisten)) doconnect(flisten);

    for (cp = connections; cp; cp = cp->next) {

      if (rmask & (1 << cp->fd))
	if ((size = read(cp->fd, buffer, sizeof(buffer))) <= 0)
	  bye_command(cp);
	else
	  for (i = 0; i < size; i++)
	    switch (buffer[i]) {
	    case '\b':
	      if (cp->icnt) cp->icnt--;
	      break;
	    case '\n':
	    case '\r':
	      cp->ibuf[cp->icnt] = '\0';
	      if (cp->icnt) {
		process_input(cp);
		cp->icnt = 0;
	      }
	      break;
	    default:
	      if (cp->icnt < sizeof(cp->ibuf) - 5)
		cp->ibuf[cp->icnt++] = buffer[i];
	      break;
	    }

      if (wmask & (1 << cp->fd)) {
	c = pullchar(&cp->obuf);
	write(cp->fd, &c, 1);
      }

    }
  }
}

