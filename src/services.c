/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/services.c,v 1.4 1992-05-26 10:09:09 deyke Exp $ */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "socket.h"
#include "netuser.h"

struct port_table {
  char  *name;
  int  port;
};

static struct port_table tcp_port_table[] = {
  "*",           0,
  "convers",     3600,
  "discard",     IPPORT_DISCARD,/* ARPA discard protocol */
  "domain",      IPPORT_DOMAIN, /* ARPA domain nameserver */
  "echo",        IPPORT_ECHO,   /* ARPA echo protocol */
  "finger",      IPPORT_FINGER, /* ARPA finger protocol */
  "ftp",         IPPORT_FTP,    /* ARPA file transfer protocol (cmd) */
  "ftp-data",    IPPORT_FTPD,   /* ARPA file transfer protocol (data) */
  "netupds",     4715,
  "nntp",        IPPORT_NNTP,
  "pop2",        IPPORT_POP,    /* Post Office Prot. v2 */
  "pop3",        110,           /* Post Office Prot. v3 */
  "smtp",        IPPORT_SMTP,   /* ARPA simple mail transfer protocol */
  "telnet",      IPPORT_TELNET, /* ARPA virtual terminal protocol */
  "ttylink",     IPPORT_TTYLINK,
  NULLCHAR
};

static struct port_table udp_port_table[] = {
  "*",           0,
  "bootpc",      IPPORT_BOOTPC,
  "bootps",      IPPORT_BOOTPS,
  "domain",      IPPORT_DOMAIN, /* ARPA domain nameserver */
  "remote",      IPPORT_REMOTE,
  "rip",         IPPORT_RIP,
  NULLCHAR
};

static char *nextstr __ARGS((void));
static char *port_name __ARGS((struct port_table *table, int port));
static int port_number __ARGS((struct port_table *table, char *name));

/*---------------------------------------------------------------------------*/

static char  *nextstr()
{

#define NUMSTR  16

  static char  strstore[NUMSTR][128];
  static int  strindex;

  strindex++;
  if (strindex >= NUMSTR) strindex = 0;
  return strstore[strindex];
}

/*---------------------------------------------------------------------------*/

static char  *port_name(table, port)
struct port_table *table;
int  port;
{
  char  *p;

  for (; table->name; table++)
    if (port == table->port) return table->name;
  p = nextstr();
  sprintf(p, "%u", port);
  return p;
}

/*---------------------------------------------------------------------------*/

char  *tcp_port_name(port)
int  port;
{
  return port_name(tcp_port_table, port);
}

/*---------------------------------------------------------------------------*/

char  *udp_port_name(port)
int  port;
{
  return port_name(udp_port_table, port);
}

/*---------------------------------------------------------------------------*/

static int  port_number(table, name)
struct port_table *table;
char  *name;
{
  int  len;

  if (!isdigit(uchar(*name))) {
    len = strlen(name);
    for (; table->name; table++)
      if (!strncmp(name, table->name, len)) return table->port;
  }
  return atoi(name);
}

/*---------------------------------------------------------------------------*/

int  tcp_port_number(name)
char  *name;
{
  return port_number(tcp_port_table, name);
}

/*---------------------------------------------------------------------------*/

int  udp_port_number(name)
char  *name;
{
  return port_number(udp_port_table, name);
}

/*---------------------------------------------------------------------------*/

char  *pinet_tcp(s)
struct socket *s;
{
  char  *p;

  p = nextstr();
  sprintf(p, "%s:%s", inet_ntoa(s->address), tcp_port_name(s->port));
  return p;
}

/*---------------------------------------------------------------------------*/

char  *pinet_udp(s)
struct socket *s;
{
  char  *p;

  p = nextstr();
  sprintf(p, "%s:%s", inet_ntoa(s->address), udp_port_name(s->port));
  return p;
}

