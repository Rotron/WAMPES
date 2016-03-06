#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "configure.h"

#ifdef HAVE_GDBM_H

#include <gdbm.h>

#define DBHOSTADDR      "/tcp/hostaddr.gdbm"
#define DBHOSTNAME      "/tcp/hostname.gdbm"
#define DOMAINFILE      "/tcp/domain.txt"
#define HOSTSFILE       "/tcp/hosts"
#define LOCALDOMAIN     "ampr.org"
#define LOCALDOMAINFILE "/tcp/domain.local"

static GDBM_FILE Dbhostaddr;
static GDBM_FILE Dbhostname;
static char origin[1024];

/*---------------------------------------------------------------------------*/

static int aton(const char *name, long *addrptr)
{

  const char * p;
  int i;
  long addr;

  if (!name || !isdigit(*name & 0xff)) return (-1);
  for (p = name; *p; p++)
    if (!isdigit(*p & 0xff) && *p != '.') return (-1);
  addr = 0;
  for (i = 24; i >= 0; i -= 8) {
    addr |= atol(name) << i;
    if (!(name = strchr(name, '.'))) break;
    name++;
    if (!*name) break;
  }
  *addrptr = addr;
  return 0;
}

/*---------------------------------------------------------------------------*/

static char *ntoa(long addr)
{
  static char buf[16];

  sprintf(buf, "%ld.%ld.%ld.%ld",
	  (addr >> 24) & 0xff,
	  (addr >> 16) & 0xff,
	  (addr >>  8) & 0xff,
	  (addr      ) & 0xff);
  return buf;
}

/*---------------------------------------------------------------------------*/

static void store_in_db(const char *name, const char *addrstr)
{

  datum daddr;
  datum dname;
  int i;
  long addr;

  if (aton(addrstr, &addr) || !addr || !~addr) return;
  dname.dptr = (char *) name;
  dname.dsize = strlen(name) + 1;
  daddr.dptr = (char *) &addr;
  daddr.dsize = sizeof(addr);
  i = gdbm_store(Dbhostaddr, dname, daddr, GDBM_INSERT);
  if (i < 0) {
    perror("gdbm_store");
    exit(1);
  }
  if (i > 0) fprintf(stderr, "duplicate name: %s\n", name);
  i = gdbm_store(Dbhostname, daddr, dname, GDBM_INSERT);
  if (i < 0) {
    perror("gdbm_store");
    exit(1);
  }
  if (i > 0) fprintf(stderr, "duplicate addr: %s\n", addrstr);
}

/*---------------------------------------------------------------------------*/

static void fix_line(char *line)
{
  for (; *line; line++) {
    if (*line == ';' || *line == '#') {
      *line = 0;
      return;
    }
    if (*line >= 'A' && *line <= 'Z') *line = tolower(*line);
  }
}

/*---------------------------------------------------------------------------*/

static char *fix_name(const char *name)
{

  int len;
  static char fullname[1024];

  if (!strcmp(name, "@"))
    strcpy(fullname, origin);
  else if (!*name || name[strlen(name)-1] == '.')
    strcpy(fullname, name);
  else
    sprintf(fullname, "%s.%s", name, origin);
  len = strlen(fullname);
  if (len && fullname[len-1] == '.') fullname[len-1] = 0;
  return fullname;
}

/*---------------------------------------------------------------------------*/

static void read_hosts_file(const char *filename)
{

  char addrstr[1024];
  char line[1024];
  char name[1024];
  FILE *fp;

  if (!(fp = fopen(filename, "r"))) {
    perror(filename);
    return;
  }
  while (fgets(line, sizeof(line), fp)) {
    fix_line(line);
    if (sscanf(line, "%s %s", addrstr, name) == 2) {
      strcat(name, ".");
      strcat(name, LOCALDOMAIN);
      store_in_db(name, addrstr);
    }
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void read_domain_file(const char *filename)
{

  char line[1024];
  char name[1024];
  char *p;
  datum daddr;
  datum dname;
  FILE *fp;
  long addr;
  static const char delim[] = " \t\n";

  strcpy(origin, LOCALDOMAIN);
  *name = 0;
  if (!(fp = fopen(filename, "r"))) {
    perror(filename);
    return;
  }
  while (fgets(line, sizeof(line), fp)) {
    fix_line(line);

    if (!strncmp(line, "$origin", 7)) {
      if ((p = strtok(line + 7, delim))) strcpy(origin, p);
      continue;
    }

    if (!(p = strtok(line, delim))) continue;

    if (!isspace(*line & 0xff)) {
      strcpy(name, p);
      p = strtok(0, delim);
    }

    while (p && (isdigit(*p & 0xff) || !strcmp(p, "in")))
      p = strtok(0, delim);
    if (!p) continue;

    if (strcmp(p, "a")) continue;

    if (!(p = strtok(0, delim))) continue;

    store_in_db(fix_name(name), p);

  }
  fclose(fp);

  strcpy(origin, LOCALDOMAIN);
  *name = 0;
  if (!(fp = fopen(filename, "r"))) {
    perror(filename);
    return;
  }
  while (fgets(line, sizeof(line), fp)) {
    fix_line(line);

    if (!strncmp(line, "$origin", 7)) {
      if ((p = strtok(line + 7, delim))) strcpy(origin, p);
      continue;
    }

    if (!(p = strtok(line, delim))) continue;

    if (!isspace(*line & 0xff)) {
      strcpy(name, p);
      p = strtok(0, delim);
    }

    while (p && (isdigit(*p & 0xff) || !strcmp(p, "in")))
      p = strtok(0, delim);
    if (!p) continue;

    if (strcmp(p, "cname")) continue;

    if (!(p = strtok(0, delim))) continue;

    dname.dptr = fix_name(p);
    dname.dsize = strlen(dname.dptr) + 1;
    daddr = gdbm_fetch(Dbhostaddr, dname);
    if (!daddr.dptr) {
      fprintf(stderr, "no such key: %s\n", dname.dptr);
      continue;
    }
    memcpy((char *) &addr, daddr.dptr, sizeof(addr));
    free(daddr.dptr);

    store_in_db(fix_name(name), ntoa(addr));

  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void qaddr(const char *name)
{

  char fullname[1024];
  datum daddr;
  datum dname;
  int len;
  long addr;

  strcpy(fullname, name);
  len = strlen(fullname);

  if (len && fullname[len-1] == '.') {
    fullname[len-1] = 0;
    dname.dptr = fullname;
    dname.dsize = strlen(fullname) + 1;
    daddr = gdbm_fetch(Dbhostaddr, dname);
    if (daddr.dptr) {
      memcpy((char *) &addr, daddr.dptr, sizeof(addr));
      free(daddr.dptr);
      printf("%s  %s\n", ntoa(addr), fullname);
    } else
      fprintf(stderr, "no such key: %s\n", fullname);
    return;
  }

  strcat(fullname, ".");
  strcat(fullname, LOCALDOMAIN);
  dname.dptr = fullname;
  dname.dsize = strlen(fullname) + 1;
  daddr = gdbm_fetch(Dbhostaddr, dname);
  if (daddr.dptr) {
    memcpy((char *) &addr, daddr.dptr, sizeof(addr));
    free(daddr.dptr);
    printf("%s  %s\n", ntoa(addr), fullname);
    return;
  }

  dname.dptr = (char *) name;
  dname.dsize = strlen(name) + 1;
  daddr = gdbm_fetch(Dbhostaddr, dname);
  if (daddr.dptr) {
    memcpy((char *) &addr, daddr.dptr, sizeof(addr));
    free(daddr.dptr);
    printf("%s  %s\n", ntoa(addr), name);
  } else
    fprintf(stderr, "no such key: %s\n", name);
}

/*---------------------------------------------------------------------------*/

static void qname(const char *addrstr)
{

  datum daddr;
  datum dname;
  long addr;

  if (aton(addrstr, &addr)) {
    fprintf(stderr, "no such key: %s\n", addrstr);
    return;
  }
  daddr.dptr = (char *) &addr;
  daddr.dsize = sizeof(addr);
  dname = gdbm_fetch(Dbhostname, daddr);
  if (dname.dptr) {
    printf("%s  %s\n", ntoa(addr), dname.dptr);
    free(dname.dptr);
  } else {
    fprintf(stderr, "no such key: %s\n", addrstr);
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  int i;

  if (argc >= 1 && strstr(*argv, "qaddr")) {
    if (!(Dbhostaddr = gdbm_open(DBHOSTADDR, 512, GDBM_READER, 0644, NULL))) {
      perror(DBHOSTADDR);
      exit(1);
    }
    for (i = 1; i < argc; i++) qaddr(argv[i]);
    gdbm_close(Dbhostaddr);
  } else if (argc >= 1 && strstr(*argv, "qname")) {
    if (!(Dbhostname = gdbm_open(DBHOSTNAME, 512, GDBM_READER, 0644, NULL))) {
      perror(DBHOSTNAME);
      exit(1);
    }
    for (i = 1; i < argc; i++) qname(argv[i]);
    gdbm_close(Dbhostname);
  } else {
    remove(DBHOSTADDR);
    remove(DBHOSTNAME);
    if (!(Dbhostname = gdbm_open(DBHOSTNAME, 512, GDBM_NEWDB, 0644, NULL))) {
      perror(DBHOSTNAME);
      exit(1);
    }
    if (!(Dbhostaddr = gdbm_open(DBHOSTADDR, 512, GDBM_NEWDB, 0644, NULL))) {
      perror(DBHOSTADDR);
      exit(1);
    }
    store_in_db("localhost", "127.0.0.1");
    read_domain_file(LOCALDOMAINFILE);
    read_hosts_file(HOSTSFILE);
    read_domain_file(DOMAINFILE);
    gdbm_close(Dbhostname);
    gdbm_close(Dbhostaddr);
  }
  return 0;
}

#else

int main(int argc, char **argv)
{
  return 0;
}

#endif
