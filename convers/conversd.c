#ifndef __lint
static char rcsid[] = "@(#) $Header: /home/deyke/tmp/cvs/tcp/convers/conversd.c,v 2.37 1993-06-17 07:27:12 deyke Exp $";
#endif

#define _HPUX_SOURCE

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#if defined(__hpux) \
 || defined(LINUX) \
 || defined(__386BSD__) \
 || defined(sun) \
 || defined(ULTRIX_RISC) \
 || defined(macII)
#include <sys/uio.h>
#ifndef MAXIOV
#define MAXIOV          16
#endif
#else
#undef  MAXIOV
#endif

#ifndef SOMAXCONN
#define SOMAXCONN       5
#endif

#ifndef S_IWOTH
#define S_IWOTH         2
#endif

#ifndef UTMP_FILE
#ifdef _PATH_UTMP
#define UTMP_FILE       _PATH_UTMP
#else
#define UTMP_FILE       "/etc/utmp"
#endif
#endif

#ifndef WNOHANG
#define WNOHANG         1
#endif

#ifdef __hpux
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

extern char *optarg;
extern int errno;
extern int optind;

#include "buildsaddr.h"
#include "strdup.h"

#define MAX_CHANNEL     32767
#define MAX_IDLETIME    (  60*60)
#define MAX_UNKNOWNTIME (  15*60)
#define MAX_WAITTIME    (3*60*60)
#define MIN_WAITTIME    (     60)

#define NULLCHAR        ((char *) 0)

struct mbuf {
  struct mbuf *m_next;          /* Linked list pointer */
  char *m_data;                 /* Active working pointer */
  int m_cnt;                    /* Number of bytes in data buffer */
};
#define NULLMBUF        ((struct mbuf *) 0)

struct host {
  char *h_name;                 /* Name of host */
  struct link *h_link;          /* Link to this host */
  struct host *h_next;          /* Linked list pointer */
};
#define NULLHOST        ((struct host *) 0)

struct user {
  char *u_name;                 /* Name of user */
  struct host *u_host;          /* Host where user is logged on */
  struct link *u_link;          /* Link to this user */
  int u_channel;                /* Channel number */
  long u_stime;                 /* Connect time */
  struct user *u_next;          /* Linked list pointer */
};
#define NULLUSER        ((struct user *) 0)

struct link {
  struct user *l_user;          /* Pointer to user if user link */
  struct host *l_host;          /* Pointer to host if host link */
  int l_locked;                 /* Set if mesg already sent */
  int l_fd;                     /* Socket descriptor */
  char l_ibuf[2048];            /* Input buffer */
  int l_icnt;                   /* Number of bytes in input buffer */
  int l_received;               /* Number of bytes received */
  struct mbuf *l_sndq;          /* Output queue */
  int l_xmitted;                /* Number of bytes transmitted */
  long l_stime;                 /* Time of last state change */
  long l_mtime;                 /* Time of last input/output */
  struct link *l_next;          /* Linked list pointer */
};
#define NULLLINK        ((struct link *) 0)

struct peer {
  char *p_socket;               /* Name of socket to connect to */
  char *p_command;              /* Optional connect command */
  long p_stime;                 /* Time of last state change */
  int p_tries;                  /* Number of connect tries */
  long p_retrytime;             /* Time of next connect try */
  struct link *p_link;          /* Pointer to link if connected */
  struct peer *p_next;          /* Linked list pointer */
};
#define NULLPEER        ((struct peer *) 0)

static struct fd_set chkread;
static void (*readfnc[FD_SETSIZE])(void *);
static void *readarg[FD_SETSIZE];

static struct fd_set chkwrite;
static void (*writefnc[FD_SETSIZE])(void *);
static void *writearg[FD_SETSIZE];

static char *conffile = "/tcp/convers.conf";
static const char progfile[] = "/tcp/conversd";
static int debug;
static int maxfd = -1;
static int min_waittime = MIN_WAITTIME;
static long currtime;
static struct host my, *hosts = &my;
static struct link *links;
static struct peer *peers;
static struct user *users;

static struct host *hostptr(const char *name);
static struct user *userptr(const char *name, struct host *hp);
static void trace(struct link *lp, const char *string);
static void send_string(struct link *lp, const char *string);
static int queuelength(const struct mbuf *mp);
static void free_closed_links(void);
static char *getarg(char *line, int all);
static char *formatline(const char *prefix, const char *text);
static char *localtimestring(long utc);
static void accept_connect_request(const int *flistenptr);
static void clear_locks(void);
static void send_user_change_msg(const char *name, const char *host, int oldchannel, int newchannel);
static void send_msg_to_user(const char *fromname, const char *toname, const char *text);
static void send_msg_to_channel(const char *fromname, int channel, const char *text);
static void send_invite_msg(const char *fromname, const char *toname, int channel);
static void connect_peers(void);
static void close_link(struct link *lp);
static void channel_command(struct link *lp);
static void help_command(struct link *lp);
static void hosts_command(struct link *lp);
static void invite_command(struct link *lp);
static void links_command(struct link *lp);
static void msg_command(struct link *lp);
static void peers_command(struct link *lp);
static void name_command(struct link *lp);
static void users_command(struct link *lp);
static void who_command(struct link *lp);
static void h_cmsg_command(struct link *lp);
static void h_host_command(struct link *lp);
static void h_invi_command(struct link *lp);
static void h_umsg_command(struct link *lp);
static void h_user_command(struct link *lp);
static void process_input(struct link *lp);
static void read_configuration(void);
static void check_files_changed(void);
static void link_recv(struct link *lp);
static void link_send(struct link *lp);

/*---------------------------------------------------------------------------*/

static struct host *hostptr(const char *name)
{

  int r;
  struct host **hpp;
  struct host *hp;

  for (hpp = &hosts; ; hpp = &hp->h_next) {
    hp = *hpp;
    if (!hp || (r = strcmp(hp->h_name, name)) >= 0) {
      if (!hp || r) {
	hp = (struct host *) calloc(1, sizeof(*hp));
	hp->h_name = strdup((char *) name);
	hp->h_next = *hpp;
	*hpp = hp;
      }
      return hp;
    }
  }
}

/*---------------------------------------------------------------------------*/

static struct user *userptr(const char *name, struct host *hp)
{

  int r;
  struct user **upp;
  struct user *up;

  for (upp = &users; ; upp = &up->u_next) {
    up = *upp;
    if (!up ||
	(r = strcmp(up->u_name, name)) > 0 ||
	!r && (r = strcmp(up->u_host->h_name, hp->h_name)) >= 0) {
      if (!up || r) {
	up = (struct user *) calloc(1, sizeof(*up));
	up->u_name = strdup((char *) name);
	up->u_host = hp;
	up->u_channel = -1;
	up->u_stime = currtime;
	up->u_next = *upp;
	*upp = up;
      }
      return up;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void trace(struct link *lp, const char *string)
{

  char *name;
  char buf[16];

  if (lp->l_user)
    name = lp->l_user->u_name;
  else if (lp->l_host)
    name = lp->l_host->h_name;
  else
    sprintf(name = buf, "%p", lp);
  printf("%s->%s: %s", my.h_name, name, string);
  fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static void send_string(struct link *lp, const char *string)
{

  int len;
  struct mbuf *mp;
  struct mbuf *p;

  if (!*string) return;
  if (debug) trace(lp, string);
  len = strlen(string);
  mp = (struct mbuf *) malloc(sizeof(*mp) + len);
  mp->m_next = 0;
  memcpy(mp->m_data = (char *) (mp + 1), string, mp->m_cnt = len);
  if (lp->l_sndq) {
    for (p = lp->l_sndq; p->m_next; p = p->m_next) ;
    p->m_next = mp;
  } else {
    lp->l_sndq = mp;
    writefnc[lp->l_fd] = (void (*)()) link_send;
    writearg[lp->l_fd] = lp;
    FD_SET(lp->l_fd, &chkwrite);
  }
  lp->l_locked = 1;
}

/*---------------------------------------------------------------------------*/

static int queuelength(const struct mbuf *mp)
{
  int len;

  for (len = 0; mp; mp = mp->m_next)
    len += mp->m_cnt;
  return len;
}

/*---------------------------------------------------------------------------*/

static void free_closed_links(void)
{

  struct link **lpp;
  struct link *lp;
  struct mbuf *mp;

  for (lpp = &links; lp = *lpp; ) {
    if (lp->l_user || lp->l_host) {
      if (lp->l_mtime + MAX_IDLETIME < currtime) send_string(lp, "\n");
    } else if (lp->l_fd >= 0) {
      if (lp->l_stime + MAX_UNKNOWNTIME < currtime) close_link(lp);
    }
    if (lp->l_fd < 0) {
      *lpp = lp->l_next;
      while (mp = lp->l_sndq) {
	lp->l_sndq = mp->m_next;
	free(mp);
      }
      free(lp);
    } else
      lpp = &lp->l_next;
  }
}

/*---------------------------------------------------------------------------*/

static char *getarg(char *line, int all)
{

  char *arg;
  static char *cp;

  if (line) {
    for (cp = line; *cp; cp++) ;
    while (--cp >= line && isspace(*cp & 0xff)) ;
    cp[1] = 0;
    cp = line;
  }
  while (isspace(*cp & 0xff)) cp++;
  if (all) return cp;
  arg = cp;
  while (*cp && !isspace(*cp & 0xff)) {
    if (*cp >= 'A' && *cp <= 'Z') *cp = tolower(*cp);
    cp++;
  }
  if (*cp) *cp++ = 0;
  return arg;
}

/*---------------------------------------------------------------------------*/

/* Returns a formatted version of the given text.
 * The prefix appears at the beginning of the text, and each
 * line after the first will be indented to column PREFIXLEN.
 * All whitespace (SPACE and TAB) will be replaced by a single
 * SPACE character,
 * Lines will be filled to be CONVLINELEN characters long, wrapping
 * words as necessary.
 */

static char *formatline(const char *prefix, const char *text)
{

#define PREFIXLEN       10
#define CONVLINELEN     79

  char *e, *t;
  const char *f, *x;
  int l, lw;
  static char buf[2048];

  e = buf + (sizeof(buf) - 2);

  /* Copy prefix into buf, set l to length of prefix */
  for (f = prefix, t = buf; *f; f++)
    if (t < e) *t++ = *f;
  l = t - buf;

  for (f = text; ; ) {

    /* Skip leading spaces */
    while (isspace(*f & 0xff)) f++;

    /* Return if nothing more or no room left */
    if (!*f || t >= e) {
      *t++ = '\n';
      *t = 0;
      return buf;
    }

    /* Find length of next word (seq. of non-blanks) */
    for (x = f; *x && !isspace(*x & 0xff); x++) ;
    lw = x - f;

    /* If the word would extend past end of line, do newline */
    if (l > PREFIXLEN && l + 1 + lw > CONVLINELEN) {
      if (t < e) *t++ = '\n';
      l = 0;
    }

    /* Put out a single space, or indent to column PREFIXLEN */
    do {
      if (t < e) *t++ = ' ';
      l++;
    } while (l < PREFIXLEN);

    /* Put out the word */
    while (lw--) {
      if (t < e) *t++ = *f++;
      l++;
    }
  }
}

/*---------------------------------------------------------------------------*/

static char *localtimestring(long utc)
{

  static char buffer[8];
  static const char monthnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  struct tm *tm;

  tm = localtime(&utc);
  if (utc + 24 * 60 * 60 > currtime)
    sprintf(buffer, " %2d:%02d", tm->tm_hour, tm->tm_min);
  else
    sprintf(buffer, "%-3.3s %2d", monthnames + 3 * tm->tm_mon, tm->tm_mday);
  return buffer;
}

/*---------------------------------------------------------------------------*/

static void accept_connect_request(const int *flistenptr)
{

  int addrlen;
  int fd;
  int flags;
  struct link *lp;
  struct sockaddr addr;

  addrlen = sizeof(addr);
  if ((fd = accept(*flistenptr, &addr, &addrlen)) < 0) return;
  if ((flags = fcntl(fd, F_GETFL, 0)) == -1 ||
      fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    close(fd);
    return;
  }
  lp = (struct link *) calloc(1, sizeof(*lp));
  lp->l_fd = fd;
  lp->l_stime = lp->l_mtime = currtime;
  lp->l_next = links;
  links = lp;
  readfnc[fd] = (void (*)()) link_recv;
  readarg[fd] = lp;
  FD_SET(fd, &chkread);
  if (maxfd < fd) maxfd = fd;
}

/*---------------------------------------------------------------------------*/

static void clear_locks(void)
{
  struct link *lp;

  for (lp = links; lp; lp = lp->l_next) lp->l_locked = 0;
}

/*---------------------------------------------------------------------------*/

static void send_user_change_msg(const char *name, const char *host, int oldchannel, int newchannel)
{

  char buffer[2048];
  struct link *lp;

  for (lp = links; lp; lp = lp->l_next) {
    if (!lp->l_locked) {
      if (lp->l_user) {
	if (lp->l_user->u_channel == oldchannel) {
	  if (newchannel >= 0)
	    sprintf(buffer, "*** %s switched to channel %d.\n", name, newchannel);
	  else
	    sprintf(buffer, "*** %s signed off.\n", name);
	  send_string(lp, buffer);
	} else if (lp->l_user->u_channel == newchannel) {
	  sprintf(buffer, "*** %s signed on.\n", name);
	  send_string(lp, buffer);
	}
      } else if (lp->l_host) {
	sprintf(buffer, "/\377\200USER %s %s %d %d %d\n", name, host, 0, oldchannel, newchannel);
	send_string(lp, buffer);
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

static void send_msg_to_user(const char *fromname, const char *toname, const char *text)
{

  char buffer[2048];
  struct link *lp;
  struct user *up;

  for (up = users; up; up = up->u_next) {
    lp = up->u_link;
    if (!lp->l_locked && !strcmp(up->u_name, toname)) {
      if (lp->l_host) {
	sprintf(buffer, "/\377\200UMSG %s %s %s\n", fromname, toname, text);
	send_string(lp, buffer);
      } else if (strcmp(fromname, "conversd")) {
	sprintf(buffer, "<*%s*>:", fromname);
	send_string(lp, formatline(buffer, text));
      } else {
	sprintf(buffer, "%s\n", text);
	send_string(lp, buffer);
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

static void send_msg_to_channel(const char *fromname, int channel, const char *text)
{

  char buffer[2048];
  struct link *lp;
  struct user *up;

  for (up = users; up; up = up->u_next) {
    lp = up->u_link;
    if (!lp->l_locked && up->u_channel == channel) {
      if (lp->l_host) {
	sprintf(buffer, "/\377\200CMSG %s %d %s\n", fromname, channel, text);
	send_string(lp, buffer);
      } else {
	sprintf(buffer, "<%s>:", fromname);
	send_string(lp, formatline(buffer, text));
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

static void send_invite_msg(const char *fromname, const char *toname, int channel)
{

  static const char invitetext[] =
  "\n\007\007*** Message from %s at %s ...\n"
  "Please join convers channel %d.\n\007\007\n";

  static const char responsetext[] =
  "*** Invitation sent to %s @ %s.";

  char buffer[2048];
  int fdtty;
  int fdut;
  struct link *lp;
  struct stat statbuf;
  struct user *up;
  struct utmp utmpbuf;

  for (up = users; up; up = up->u_next)
    if (up->u_channel == channel && !strcmp(up->u_name, toname)) {
      clear_locks();
      sprintf(buffer, "*** User %s is already on this channel.", toname);
      send_msg_to_user("conversd", fromname, buffer);
      return;
    }

  for (up = users; up; up = up->u_next)
    if (up->u_link->l_user && !strcmp(up->u_name, toname)) {
      sprintf(buffer, invitetext, fromname, localtimestring(currtime), channel);
      send_string(up->u_link, buffer);
      clear_locks();
      sprintf(buffer, responsetext, toname, my.h_name);
      send_msg_to_user("conversd", fromname, buffer);
      return;
    }

  for (up = users; up; up = up->u_next)
    if (!up->u_link->l_locked && !strcmp(up->u_name, toname)) {
      sprintf(buffer, "/\377\200INVI %s %s %d\n", fromname, toname, channel);
      send_string(up->u_link, buffer);
      return;
    }

  if ((fdut = open(UTMP_FILE, O_RDONLY, 0644)) >= 0) {
    while (read(fdut, &utmpbuf, sizeof(utmpbuf)) == sizeof(utmpbuf))
      if (
#ifdef USER_PROCESS
	  utmpbuf.ut_type == USER_PROCESS &&
#endif
	  !strncmp(utmpbuf.ut_name, toname, sizeof(utmpbuf.ut_name))) {
	strcpy(buffer, "/dev/");
	strncat(buffer, utmpbuf.ut_line, sizeof(utmpbuf.ut_line));
	if (stat(buffer, &statbuf)) continue;
	if (!(statbuf.st_mode & S_IWOTH)) continue;
	if ((fdtty = open(buffer, O_WRONLY | O_NOCTTY, 0644)) < 0) continue;
	sprintf(buffer, invitetext, fromname, localtimestring(currtime), channel);
	if (!fork()) {
	  write(fdtty, buffer, strlen(buffer));
	  _exit(0);
	}
	close(fdtty);
	close(fdut);
	clear_locks();
	sprintf(buffer, responsetext, toname, my.h_name);
	send_msg_to_user("conversd", fromname, buffer);
	return;
      }
    close(fdut);
  }

  for (lp = links; lp; lp = lp->l_next)
    if (lp->l_host && !lp->l_locked) {
      sprintf(buffer, "/\377\200INVI %s %s %d\n", fromname, toname, channel);
      send_string(lp, buffer);
    }

}

/*---------------------------------------------------------------------------*/

static void connect_peers(void)
{

  char *cp1;
  char *cp;
  char buffer[2048];
  int addrlen;
  int fd;
  int flags;
  int wt;
  struct link *lp;
  struct peer *pp;
  struct sockaddr *addr;

  for (pp = peers; pp; pp = pp->p_next) {
    if (pp->p_link || pp->p_retrytime > currtime) continue;
    wt = min_waittime * (1 << pp->p_tries);
    pp->p_tries++;
    if (pp->p_tries >= 16 || wt > MAX_WAITTIME) wt = MAX_WAITTIME;
    pp->p_retrytime = currtime + wt;
    if (!(addr = build_sockaddr(pp->p_socket, &addrlen))) continue;
    if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) continue;
    if ((flags = fcntl(fd, F_GETFL, 0)) == -1 ||
	fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      close(fd);
      continue;
    }
    if (connect(fd, addr, addrlen) && errno != EINPROGRESS) {
      if (debug) perror("connect()");
      close(fd);
      continue;
    }
    lp = pp->p_link = (struct link *) calloc(1, sizeof(*lp));
    lp->l_fd = fd;
    lp->l_stime = lp->l_mtime = currtime;
    lp->l_next = links;
    links = lp;
    readfnc[fd] = (void (*)()) link_recv;
    readarg[fd] = lp;
    FD_SET(fd, &chkread);
    if (maxfd < fd) maxfd = fd;
    *buffer = 0;
    cp = pp->p_command;
    while (*cp) {
      if (cp1 = strstr(cp, "\\n")) {
	strncat(buffer, cp, cp1 - cp);
	cp = cp1 + 2;
      } else {
	strcat(buffer, cp);
	cp = "";
      }
      strcat(buffer, "\n");
    }
    strcat(buffer, "convers\n/\377\200HOST ");
    strcat(buffer, my.h_name);
    strcat(buffer, "\n");
    send_string(lp, buffer);
  }
}

/*---------------------------------------------------------------------------*/

static void close_link(struct link *lp)
{

  int fd;
  struct host *hp;
  struct peer *pp;
  struct user **upp;
  struct user *up;

  lp->l_user = 0;
  lp->l_host = 0;

  if ((fd = lp->l_fd) >= 0) {
    readfnc[fd] = 0;
    FD_CLR(fd, &chkread);
    writefnc[fd] = 0;
    FD_CLR(fd, &chkwrite);
    if (fd == maxfd)
      while (--maxfd >= 0)
	if (FD_ISSET(maxfd, &chkread)) break;
    close(fd);
    lp->l_fd = -1;
  }

  for (pp = peers; pp; pp = pp->p_next)
    if (pp->p_link == lp) {
      pp->p_stime = currtime;
      if (pp->p_retrytime < currtime + min_waittime)
	pp->p_retrytime = currtime + min_waittime;
      pp->p_link = 0;
    }

  for (upp = &users; up = *upp; )
    if (up->u_link == lp) {
      clear_locks();
      send_user_change_msg(up->u_name, up->u_host->h_name, up->u_channel, -1);
      *upp = up->u_next;
      free(up->u_name);
      free(up);
    } else
      upp = &up->u_next;

  for (hp = hosts; hp; hp = hp->h_next)
    if (hp->h_link == lp) hp->h_link = 0;

}

/*---------------------------------------------------------------------------*/

static void channel_command(struct link *lp)
{

  char *cp;
  char buffer[2048];
  int newchannel;
  struct user *up;

  up = lp->l_user;
  cp = getarg(NULLCHAR, 0);
  if (!*cp) {
    sprintf(buffer, "*** You are on channel %d.\n", up->u_channel);
    send_string(lp, buffer);
    return;
  }
  newchannel = atoi(cp);
  if (newchannel < 0 || newchannel > MAX_CHANNEL) {
    sprintf(buffer, "*** Channel numbers must be in the range 0..%d.\n", MAX_CHANNEL);
    send_string(lp, buffer);
    return;
  }
  if (newchannel == up->u_channel) {
    sprintf(buffer, "*** Already on channel %d.\n", up->u_channel);
    send_string(lp, buffer);
    return;
  }
  send_user_change_msg(up->u_name, up->u_host->h_name, up->u_channel, newchannel);
  up->u_channel = newchannel;
  sprintf(buffer, "*** Now on channel %d.\n", up->u_channel);
  send_string(lp, buffer);
}

/*---------------------------------------------------------------------------*/

static void help_command(struct link *lp)
{
  send_string(lp,
    "Commands may be abbreviated.  Commands are:\n"

    "/?                   Show help information\n"
    "/HELP                Show help information\n"

    "/BYE                 Terminate the convers session\n"
    "/EXIT                Terminate the convers session\n"
    "/QUIT                Terminate the convers session\n"

    "/WHO                 Show active channels and users\n"

    "/CHANNEL n           Switch to channel n\n"

    "/MSG user text...    Send a private message to user\n"
    "/WRITE user text...  Send a private message to user\n"

    "/INVITE user         Invite user to join your channel\n"

    "/HOSTS               Show all hosts\n"
    "/LINKS               Show all links\n"
    "/PEERS               Show all peers\n"
    "/USERS               Show all users\n"

    "***\n");
}

/*---------------------------------------------------------------------------*/

static void hosts_command(struct link *lp)
{

  char buffer[2048];
  struct host *hp;

  send_string(lp, "Name     Via\n");
  for (hp = hosts; hp; hp = hp->h_next) {
    sprintf(buffer,
	    "%-8.8s %-8.8s\n",
	    hp->h_name,
	    hp->h_link ? hp->h_link->l_host->h_name : "");
    send_string(lp, buffer);
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void invite_command(struct link *lp)
{
  char *toname;

  toname = getarg(NULLCHAR, 0);
  if (*toname) send_invite_msg(lp->l_user->u_name, toname, lp->l_user->u_channel);
}

/*---------------------------------------------------------------------------*/

static void links_command(struct link *lp)
{

  char *name;
  char *state;
  char buffer[2048];
  struct link *p;

  send_string(lp, "Name     State      Since  Idle Queue Receivd Xmitted\n");
  for (p = links; p; p = p->l_next) {
    if (p->l_user) {
      state = "User";
      name = p->l_user->u_name;
    } else if (p->l_host) {
      state = "Host";
      name = p->l_host->h_name;
    } else if (p->l_fd >= 0) {
      state = "Conn pend";
      name = "";
    } else {
      state = "Closed";
      name = "";
    }
    sprintf(buffer,
	    "%-8.8s %-9s %s %5d %5d %7d %7d\n",
	    name,
	    state,
	    localtimestring(p->l_stime),
	    currtime - p->l_mtime,
	    queuelength(p->l_sndq),
	    p->l_received,
	    p->l_xmitted);
    send_string(lp, buffer);
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void msg_command(struct link *lp)
{

  char *text;
  char *toname;
  char buffer[2048];
  struct user *up;

  toname = getarg(NULLCHAR, 0);
  text = getarg(NULLCHAR, 1);
  if (!*text) return;
  for (up = users; up; up = up->u_next)
    if (!strcmp(up->u_name, toname)) {
      send_msg_to_user(lp->l_user->u_name, toname, text);
      return;
    }
  sprintf(buffer, "*** No such user: %s.\n", toname);
  send_string(lp, buffer);
}

/*---------------------------------------------------------------------------*/

static void peers_command(struct link *lp)
{

  char *name;
  char *state;
  char buffer[2048];
  char nexttry[8];
  struct peer *pp;

  send_string(lp, "Address                                 State      Since NextTry Tries LinkedTo\n");
  for (pp = peers; pp; pp = pp->p_next) {
    strcpy(nexttry, "      ");
    if (!pp->p_link) {
      state = "Wait";
      name = "";
      if (pp->p_retrytime > currtime)
	strcpy(nexttry, localtimestring(pp->p_retrytime));
    } else if (pp->p_link->l_user) {
      state = "User";
      name = pp->p_link->l_user->u_name;
    } else if (pp->p_link->l_host) {
      state = "Connected";
      name = pp->p_link->l_host->h_name;
    } else if (pp->p_link->l_fd >= 0) {
      state = "Conn pend";
      name = "";
    } else {
      state = "Closed";
      name = "";
    }
    sprintf(buffer,
	    "%-39.39s %-9s %s  %s %5d %-8.8s\n",
	    *pp->p_command ? pp->p_command : pp->p_socket,
	    state,
	    localtimestring(pp->p_stime),
	    nexttry,
	    pp->p_tries,
	    name);
    send_string(lp, buffer);
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void name_command(struct link *lp)
{

  char *name;
  char buffer[2048];
  int oldchannel;
  struct link *lpold;
  struct user *up;

  name = getarg(NULLCHAR, 0);
  if (!*name) return;
  up = userptr(name, &my);
  oldchannel = up->u_channel;
  lpold = up->u_link;
  up->u_link = lp;
  if (lpold) close_link(lpold);
  lp->l_user = up;
  lp->l_stime = currtime;
  sprintf(buffer, "conversd @ %s $Revision: 2.37 $  Type /HELP for help.\n", my.h_name);
  send_string(lp, buffer);
  up->u_channel = atoi(getarg(NULLCHAR, 0));
  if (up->u_channel < 0 || up->u_channel > MAX_CHANNEL) {
    sprintf(buffer, "*** Channel numbers must be in the range 0..%d.\n", MAX_CHANNEL);
    send_string(lp, buffer);
    up->u_channel = 0;
  }
  if (oldchannel != up->u_channel)
    send_user_change_msg(name, my.h_name, oldchannel, up->u_channel);
}

/*---------------------------------------------------------------------------*/

static void users_command(struct link *lp)
{

  char buffer[2048];
  struct user *up;

  send_string(lp, "User     Host     Channel   Time\n");
  for (up = users; up; up = up->u_next) {
    sprintf(buffer,
	    "%-8.8s %-8.8s %7d %s\n",
	    up->u_name,
	    up->u_host->h_name,
	    up->u_channel,
	    localtimestring(up->u_stime));
    send_string(lp, buffer);
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void who_command(struct link *lp)
{

  char buffer[2048];
  int channel;
  int nextchannel;
  struct user *up;

  send_string(lp, "Channel Users\n");
  nextchannel = MAX_CHANNEL + 1;
  for (up = users; up; up = up->u_next)
    if (up->u_channel < nextchannel) nextchannel = up->u_channel;
  while (nextchannel <= MAX_CHANNEL) {
    channel = nextchannel;
    nextchannel = MAX_CHANNEL + 1;
    *buffer = 0;
    for (up = users; up; up = up->u_next) {
      if (up->u_channel > channel && up->u_channel < nextchannel)
	nextchannel = up->u_channel;
      else if (up->u_channel == channel) {
	if (!*buffer) sprintf(buffer, "%7d", channel);
	strcat(buffer, " ");
	strcat(buffer, up->u_name);
      }
    }
    strcat(buffer, "\n");
    send_string(lp, buffer);
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void h_cmsg_command(struct link *lp)
{

  char *name;
  char *text;
  int channel;

  name = getarg(NULLCHAR, 0);
  channel = atoi(getarg(NULLCHAR, 0));
  text = getarg(NULLCHAR, 1);
  if (*text) send_msg_to_channel(name, channel, text);
}

/*---------------------------------------------------------------------------*/

static void h_host_command(struct link *lp)
{

  char *name;
  char buffer[2048];
  struct host *hp;
  struct link *lpold;
  struct peer *pp;
  struct user *up;

  name = getarg(NULLCHAR, 0);
  if (!*name) return;
  hp = hostptr(name);
  if (hp == &my) {
    close_link(lp);
    return;
  }
  hp->h_link = lp;
  lp->l_host = hp;
  lp->l_stime = currtime;

  for (lpold = links; lpold; lpold = lpold->l_next)
    if (lpold != lp && lpold->l_host == hp) {
      for (pp = peers; pp; pp = pp->p_next)
	if (pp->p_link == lpold) pp->p_link = lp;
      for (up = users; up; up = up->u_next)
	if (up->u_link == lpold) up->u_link = lp;
      close_link(lpold);
    }

  for (pp = peers; pp; pp = pp->p_next)
    if (pp->p_link == lp) {
      pp->p_stime = currtime;
      pp->p_tries = 0;
      pp->p_retrytime = currtime + min_waittime;
    }

  sprintf(buffer, "/\377\200HOST %s\n", my.h_name);
  send_string(lp, buffer);

  for (up = users; up; up = up->u_next) {
    sprintf(buffer, "/\377\200USER %s %s %d %d %d\n", up->u_name, up->u_host->h_name, 0, -1, up->u_channel);
    send_string(lp, buffer);
  }

}

/*---------------------------------------------------------------------------*/

static void h_invi_command(struct link *lp)
{

  char *channel;
  char *fromname;
  char *toname;

  fromname = getarg(NULLCHAR, 0);
  toname = getarg(NULLCHAR, 0);
  channel = getarg(NULLCHAR, 0);
  if (*channel) send_invite_msg(fromname, toname, atoi(channel));
}

/*---------------------------------------------------------------------------*/

static void h_umsg_command(struct link *lp)
{

  char *fromname;
  char *text;
  char *toname;

  fromname = getarg(NULLCHAR, 0);
  toname = getarg(NULLCHAR, 0);
  text = getarg(NULLCHAR, 1);
  if (*text) send_msg_to_user(fromname, toname, text);
}

/*---------------------------------------------------------------------------*/

static void h_user_command(struct link *lp)
{

  char *channel;
  char *host;
  char *name;
  char buffer[2048];
  int newchannel;
  struct host *hp;
  struct user **upp;
  struct user *up;

  name = getarg(NULLCHAR, 0);
  host = getarg(NULLCHAR, 0);
  getarg(NULLCHAR, 0); /*** ignored, protocol has changed ***/
  getarg(NULLCHAR, 0); /*** oldchannel is ignored, protocol has changed ***/
  channel = getarg(NULLCHAR, 0);
  if (!*channel) return;
  newchannel = atoi(channel);
  hp = hostptr(host);
  up = userptr(name, hp);

  if (up->u_channel != newchannel)
    if (hp == &my) {
      sprintf(buffer, "/\377\200USER %s %s %d %d %d\n", name, host, 0, newchannel, up->u_channel);
      send_string(lp, buffer);
    } else {
      send_user_change_msg(name, host, up->u_channel, newchannel);
      up->u_channel = newchannel;
      up->u_link = hp->h_link = lp;
    }

  for (upp = &users; up = *upp; )
    if (up->u_channel < 0) {
      *upp = up->u_next;
      free(up->u_name);
      free(up);
    } else
      upp = &up->u_next;
}

/*---------------------------------------------------------------------------*/

static void process_input(struct link *lp)
{

  struct command {
    const char *c_name;
    void (*c_fnc)(struct link *);
  };

  static const struct command login_commands[] = {

    "\377\200host",     h_host_command,
    "name",             name_command,

    0,                  0
  };

  static const struct command user_commands[] = {

    "?",                help_command,
    "bye",              close_link,
    "channel",          channel_command,
    "exit",             close_link,
    "help",             help_command,
    "hosts",            hosts_command,
    "invite",           invite_command,
    "links",            links_command,
    "msg",              msg_command,
    "peers",            peers_command,
    "quit",             close_link,
    "users",            users_command,
    "who",              who_command,
    "write",            msg_command,

    0,                  0
  };

  static const struct command host_commands[] = {

    "\377\200cmsg",     h_cmsg_command,
    "\377\200invi",     h_invi_command,
    "\377\200umsg",     h_umsg_command,
    "\377\200user",     h_user_command,

    0,                  0
  };

  char *arg;
  char buffer[2048];
  const struct command * cp;
  int arglen;

  clear_locks();
  lp->l_locked = 1;

  if (*lp->l_ibuf == '/') {
    arglen = strlen(arg = getarg(lp->l_ibuf + 1, 0));
    if (lp->l_user)
      cp = user_commands;
    else if (lp->l_host)
      cp = host_commands;
    else
      cp = login_commands;
    for (; cp->c_name; cp++)
      if (!strncmp(cp->c_name, arg, arglen)) {
	(*cp->c_fnc)(lp);
	return;
      }
    if (lp->l_user) {
      sprintf(buffer, "*** Unknown command '/%s'.  Type /HELP for help.\n", arg);
      send_string(lp, buffer);
    }
  } else if (lp->l_user)
    send_msg_to_channel(lp->l_user->u_name, lp->l_user->u_channel, getarg(lp->l_ibuf, 1));
}

/*---------------------------------------------------------------------------*/

static void read_configuration(void)
{

  FILE * fp;
  char *cp;
  char *host_name;
  char *sock_name;
  char line[1024];
  struct peer *pp;

  if (fp = fopen(conffile, "r")) {
    if (fgets(line, sizeof(line), fp)) {
      if (cp = strchr(line, '#')) *cp = 0;
      host_name = getarg(line, 0);
      if (*host_name) {
	free(my.h_name);
	my.h_name = strdup(host_name);
      }
    }
    while (fgets(line, sizeof(line), fp)) {
      if (cp = strchr(line, '#')) *cp = 0;
      getarg(line, 0);  /*** ignore first field ***/
      sock_name = getarg(NULLCHAR, 0);
      if (*sock_name) {
	pp = (struct peer *) calloc(1, sizeof(*pp));
	pp->p_socket = strdup(sock_name);
	pp->p_command = strdup(getarg(NULLCHAR, 1));
	pp->p_stime = currtime;
	pp->p_retrytime = currtime + min_waittime;
	pp->p_next = peers;
	peers = pp;
      }
    }
    fclose(fp);
  }
}

/*---------------------------------------------------------------------------*/

static void check_files_changed(void)
{

  static long conftime;
  static long nexttime;
  static long progtime;
  struct stat statbuf;

  if (nexttime > currtime) return;
  nexttime = currtime + 600;

  if (!stat(progfile, &statbuf)) {
    if (!progtime) progtime = statbuf.st_mtime;
    if (progtime != statbuf.st_mtime && statbuf.st_mtime + 60 < currtime)
      exit(0);
  }
  if (!stat(conffile, &statbuf)) {
    if (!conftime) conftime = statbuf.st_mtime;
    if (conftime != statbuf.st_mtime && statbuf.st_mtime + 60 < currtime)
      exit(0);
  }
}

/*---------------------------------------------------------------------------*/

static void link_recv(struct link *lp)
{

  char buffer[2048];
  int i;
  int n;

  n = read(lp->l_fd, buffer, sizeof(buffer));
  if (n <= 0)
    close_link(lp);
  else {
    lp->l_mtime = currtime;
    lp->l_received += n;
    for (i = 0; i < n; i++)
      switch (buffer[i]) {
      case '\b':
	if (lp->l_icnt) lp->l_icnt--;
	break;
      case '\n':
      case '\r':
	if (lp->l_icnt) {
	  lp->l_ibuf[lp->l_icnt] = 0;
	  lp->l_icnt = 0;
	  process_input(lp);
	}
	break;
      default:
	if (lp->l_icnt < sizeof(lp->l_ibuf) - 5) lp->l_ibuf[lp->l_icnt++] = buffer[i];
	break;
      }
  }
}

/*---------------------------------------------------------------------------*/

static void link_send(struct link *lp)
{

  int n;
  struct mbuf *mp;

#ifdef MAXIOV

  struct iovec iov[MAXIOV];

  n = 0;
  for (mp = lp->l_sndq; mp && n < MAXIOV; mp = mp->m_next) {
    iov[n].iov_base = mp->m_data;
    iov[n].iov_len = mp->m_cnt;
    n++;
  }
  n = writev(lp->l_fd, iov, n);

#else

  n = write(lp->l_fd, lp->l_sndq->m_data, lp->l_sndq->m_cnt);

#endif

  if (n <= 0)
    close_link(lp);
  else {
    lp->l_mtime = currtime;
    lp->l_xmitted += n;
    while (n > 0) {
      if (n >= lp->l_sndq->m_cnt) {
	n -= lp->l_sndq->m_cnt;
	mp = lp->l_sndq;
	lp->l_sndq = mp->m_next;
	free(mp);
      } else {
	lp->l_sndq->m_data += n;
	lp->l_sndq->m_cnt -= n;
	n = 0;
      }
    }
  }
  if (!lp->l_sndq) FD_CLR(lp->l_fd, &chkwrite);
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  static struct listeners {
    const char *s_name;
    int s_fd;
  } listeners[] = {
    "unix:/tcp/sockets/convers",        -1,
    "*:3600",                           -1,
    0,                                  -1
  };

  char *cp;
  char buffer[256];
  int addrlen;
  int arg;
  int chr;
  int errflag = 0;
  int i;
  int status;
  struct fd_set actread;
  struct fd_set actwrite;
  struct listeners *sp;
  struct sockaddr *addr;
  struct timeval timeout;

  while ((chr = getopt(argc, argv, "a:c:d")) != EOF)
    switch (chr) {
    case 'a':
      listeners[0].s_name = optarg;
      listeners[1].s_name = 0;
      break;
    case 'c':
      conffile = optarg;
      break;
    case 'd':
      debug = 1;
      min_waittime = 1;
      break;
    case '?':
      errflag = 1;
      break;
    }

  if (errflag || optind < argc) {
    fprintf(stderr, "Usage: conversd [-a address] [-c conffile] [-d]\n");
    exit(1);
  }

  umask(022);

  close(0);
  for (i = debug ? 3 : 1; i < FD_SETSIZE; i++) close(i);

  chdir("/");
  setsid();
  signal(SIGPIPE, SIG_IGN);
  if (!getenv("TZ")) putenv("TZ=MEZ-1MESZ");

  gethostname(buffer, sizeof(buffer));
  if (cp = strchr(buffer, '.')) *cp = 0;
  my.h_name = strdup(buffer);

  time(&currtime);

  read_configuration();

  for (sp = listeners; sp->s_name; sp++)
    if ((addr = build_sockaddr(sp->s_name, &addrlen)) &&
	(sp->s_fd = socket(addr->sa_family, SOCK_STREAM, 0)) >= 0) {
      switch (addr->sa_family) {
      case AF_UNIX:
	unlink(addr->sa_data);
	break;
      case AF_INET:
	arg = 1;
	setsockopt(sp->s_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
	break;
      }
      if (bind(sp->s_fd, addr, addrlen) || listen(sp->s_fd, SOMAXCONN)) {
	close(sp->s_fd);
	sp->s_fd = -1;
      } else {
	readfnc[sp->s_fd] = (void (*)()) accept_connect_request;
	readarg[sp->s_fd] = &sp->s_fd;
	FD_SET(sp->s_fd, &chkread);
	if (maxfd < sp->s_fd) maxfd = sp->s_fd;
      }
    }

  for (; ; ) {
    waitpid(-1, &status, WNOHANG);
    free_closed_links();
    check_files_changed();
    connect_peers();
    actread = chkread;
    actwrite = chkwrite;
    timeout.tv_sec = min_waittime;
    timeout.tv_usec = 0;
    i = select(maxfd + 1, SEL_ARG(&actread), SEL_ARG(&actwrite), SEL_ARG(0), &timeout);
    time(&currtime);
    if (i > 0)
      for (i = maxfd; i >= 0; i--) {
	if (readfnc [i] && FD_ISSET(i, &actread )) (*readfnc [i])(readarg [i]);
	if (writefnc[i] && FD_ISSET(i, &actwrite)) (*writefnc[i])(writearg[i]);
      }
  }
}

