#ifndef __lint
static const char rcsid[] = "@(#) $Header: /home/deyke/tmp/cvs/tcp/tools/mm2html.c,v 1.5 1995-05-09 21:13:05 deyke Exp $";
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DO_TABLES       1

#define HEADERLEVELS    6
#define LISTLEVELS      6

struct numbers {
  struct numbers *next;
  char *name;
  int value;
};

struct headers {
  struct headers *next;
  int level;
  char *label;
  char *text;
  int in_toc;
  int linenum;
};

static FILE *fileptr;
static char title[1024];
static int afterheader;
static int aftertitle;
static int centered;
static int headercnt[HEADERLEVELS];
static int headerlevel;
static int linenum;
static int listlevel;
static int listtype[LISTLEVELS];
static int pass;
static struct headers *headers;
static struct headers *headerstail;
static struct numbers *numbers;

/*---------------------------------------------------------------------------*/

static void error(const char *msg)
{
  fprintf(stderr, "*** ERROR in line %d: %s\n", linenum, msg);
}

/*---------------------------------------------------------------------------*/

static void rip(char *s)
{
  char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && (*p == '\r' || *p == '\n')) ;
  p[1] = 0;
}

/*---------------------------------------------------------------------------*/

static void setnumber(const char *name, int value)
{
  struct numbers *p;

  for (p = numbers; p; p = p->next) {
    if (!strcmp(p->name, name)) {
      p->value = value;
      return;
    }
  }
  p = (struct numbers *) malloc(sizeof(struct numbers));
  p->next = numbers;
  numbers = p;
  p->name = strdup(name);
  p->value = value;
}

/*---------------------------------------------------------------------------*/

static int getnumber(const char *name)
{
  struct numbers *p;

  for (p = numbers; p; p = p->next) {
    if (!strcmp(p->name, name)) {
      return p->value;
    }
  }
  error("unknown register");
  return 0;
}

/*---------------------------------------------------------------------------*/

static struct headers *find_header_text(const char *text)
{

  struct headers *besthp;
  struct headers *hp;

  besthp = 0;
  for (hp = headers; hp; hp = hp->next)
    if (!strcmp(hp->text, text) &&
	(!besthp ||
	 abs(besthp->linenum - linenum) > abs(hp->linenum - linenum)))
      besthp = hp;
  return besthp;
}

/*---------------------------------------------------------------------------*/

static char *escape_special_characters(char *outbuf, const char *inbuf)
{

  char *cp;
  char *tp;
  char *tpsave;
  char textbuf[2048];
  const char *fp;
  int fontswitch;
  struct headers *hp;

  fontswitch = 0;
  tpsave = 0;
  for (fp = inbuf, tp = outbuf; *fp; fp++)
    switch (*fp) {

    case '"':
      *tp++ = '&';
      *tp++ = 'q';
      *tp++ = 'u';
      *tp++ = 'o';
      *tp++ = 't';
      *tp++ = ';';
      break;

    case '&':
      *tp++ = '&';
      *tp++ = 'a';
      *tp++ = 'm';
      *tp++ = 'p';
      *tp++ = ';';
      break;

    case '<':
      *tp++ = '&';
      *tp++ = 'l';
      *tp++ = 't';
      *tp++ = ';';
      break;

    case '>':
      *tp++ = '&';
      *tp++ = 'g';
      *tp++ = 't';
      *tp++ = ';';
      break;

    case '\\':
      switch (fp[1]) {

      case 'f':
	switch (fp[2]) {

	case 'B':
	case 'I':
	  if (fontswitch)
	    error("font nesting error");
	  tpsave = tp;
	  tp = textbuf;
	  fp += 2;
	  fontswitch = *fp;
	  break;

	case 'P':
	  if (!fontswitch) {
	    error("font nesting error");
	  } else {
	    *tp = 0;
	    tp = tpsave;
	  }
	  if (fontswitch == 'B' && (hp = find_header_text(textbuf))) {
	    *tp++ = '<';
	    *tp++ = 'A';
	    *tp++ = ' ';
	    *tp++ = 'H';
	    *tp++ = 'R';
	    *tp++ = 'E';
	    *tp++ = 'F';
	    *tp++ = '=';
	    *tp++ = '"';
	    *tp++ = '#';
	    for (cp = hp->label; *cp; cp++)
	      *tp++ = *cp;
	    *tp++ = '"';
	    *tp++ = '>';
	    for (cp = hp->text; *cp; cp++)
	      *tp++ = *cp;
	    *tp++ = '<';
	    *tp++ = '/';
	    *tp++ = 'A';
	    *tp++ = '>';
	  } else {
	    *tp++ = '<';
	    *tp++ = fontswitch;
	    *tp++ = '>';
	    for (cp = textbuf; *cp; cp++)
	      *tp++ = *cp;
	    *tp++ = '<';
	    *tp++ = '/';
	    *tp++ = fontswitch;
	    *tp++ = '>';
	  }
	  fp += 2;
	  fontswitch = 0;
	  break;

	default:
	  error("unknown \\ sequence");
	  break;

	}
	break;

      default:
	error("unknown \\ sequence");
	break;

      }
      break;

    default:
      *tp++ = *fp;
      break;

    }
  *tp = 0;
  if (fontswitch)
    error("font nesting error");
  return outbuf;
}

/*---------------------------------------------------------------------------*/

static void print_contents(void)
{

  int lastlevel;
  struct headers *hp;

  if (headers) {
    lastlevel = 0;
    puts("<HR><H2>Table of Contents</H2>");
    for (hp = headers; hp; hp = hp->next) {
      if (hp->in_toc) {
	while (lastlevel < hp->level) {
	  puts("<OL>");
	  lastlevel++;
	}
	while (lastlevel > hp->level) {
	  puts("</OL>");
	  lastlevel--;
	}
	printf("<LI><A HREF=\"#%s\">%s</A>\n", hp->label, hp->text);
      }
    }
    while (lastlevel > 0) {
      puts("</OL>");
      lastlevel--;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void start_list(int type)
{
  if (listlevel < LISTLEVELS)
    printf("<%cL>\n", listtype[listlevel++] = type);
  else
    error("list nesting error");
}

/*---------------------------------------------------------------------------*/

static void dot_AL(int argc, char **argv)
{
  start_list('O');
}

/*---------------------------------------------------------------------------*/

static void dot_BL(int argc, char **argv)
{
  start_list('U');
}

/*---------------------------------------------------------------------------*/

static void dot_LI(int argc, char **argv)
{
  if (listlevel <= 0) {
    error("list nesting error");
    return;
  }
  switch (listtype[listlevel - 1]) {
  case 'D':
    if (argc != 2) {
      error("incorrect argument count");
      return;
    }
    printf("<DT>%s\n<DD>", argv[1]);
    break;
  case 'O':
    printf("<LI>");
    if (argc != 1)
      error("incorrect argument count");
    break;
  case 'U':
    printf("<LI>");
    if (argc != 1)
      error("incorrect argument count");
    break;
  default:
    error("unknown list type");
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void dot_LE(int argc, char **argv)
{
  if (listlevel <= 0) {
    error("list nesting error");
    return;
  }
  printf("</%cL>\n", listtype[--listlevel]);
}

/*---------------------------------------------------------------------------*/

static void dot_DS(int argc, char **argv)
{
  puts("<PRE>");
}

/*---------------------------------------------------------------------------*/

static void dot_DE(int argc, char **argv)
{
  puts("</PRE>");
}

/*---------------------------------------------------------------------------*/

static void dot_H(int argc, char **argv)
{

  char *cp;
  char label[1024];
  int i;
  int level;
  struct headers *hp;

  if (argc < 3 || argc > 4) {
    error("incorrect argument count");
    return;
  }
  level = atoi(argv[1]);
  if (level < 1 || level > HEADERLEVELS) {
    error("incorrect header level");
    return;
  }
  if (level > headerlevel + 1)
    error("incorrect header level");
  while (headerlevel < level)
    headercnt[headerlevel++] = 0;
  headerlevel = level;
  headercnt[level - 1]++;
  cp = label;
  *cp++ = 'H';
  for (i = 0; i < level; i++) {
    sprintf(cp, "%d", headercnt[i]);
    while (*cp)
      cp++;
    if (level == 1 || i < level - 1)
      *cp++ = '.';
  }
  *cp = 0;
  switch (pass) {
  case 1:
    hp = (struct headers *) malloc(sizeof(struct headers));
    hp->next = 0;
    hp->level = level;
    hp->label = strdup(label);
    hp->text = strdup(argv[2]);
    hp->in_toc = (level <= getnumber("Cl"));
    hp->linenum = linenum;
    if (!headers)
      headers = hp;
    else
      headerstail->next = hp;
    headerstail = hp;
    break;
  case 2:
    if (!afterheader) {
      print_contents();
      afterheader = 1;
    }
    if (level == 1)
      printf("<HR>");
    printf("<H%d><A NAME=\"%s\">%s %s", level + 1, label, label + 1, argv[2]);
    if (argc > 3)
      printf("%s", argv[3]);
    printf("</A></H%d>\n", level + 1);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void dot_P(int argc, char **argv)
{
  puts("<P>");
}

/*---------------------------------------------------------------------------*/

static void dot_SP(int argc, char **argv)
{
  puts("<P>");
}

/*---------------------------------------------------------------------------*/

static void dot_TS(int argc, char **argv)
{

#if DO_TABLES

  char buf[1024];
  char data_align_flgs[1024];
  char header_align_flgs[1024];
  char line[1024];
  char *align_flgs;
  char *align_str;
  char *cp;
  char *l1;
  char *l;
  char *tag;
  int borderflg;
  int columns;
  int i;
  int tab;

  /* global options; */

  borderflg = 0;
  tab = '\t';
  while (fgets(line, sizeof(line), fileptr)) {
    if (strstr(line, "box"))
      borderflg = 1;
    if ((cp = strstr(line, "tab(")))
      tab = cp[4];
    if (strstr(line, ";\n"))
      break;
  }

  /* column descriptors. */

  columns = 0;
  align_flgs = header_align_flgs;
  while (fgets(line, sizeof(line), fileptr)) {
    strcpy(buf, line);
    columns = 0;
    for (cp = strtok(buf, " .|\n"); cp; cp = strtok(0, " .|\n")) {
      align_flgs[columns] = 0;
      if (strchr(cp, 'l'))
	align_flgs[columns] = 'l';
      if (strchr(cp, 'r'))
	align_flgs[columns] = 'r';
      if (strchr(cp, 'c'))
	align_flgs[columns] = 'c';
      if (strchr(cp, 'n'))
	align_flgs[columns] = 'r';
      columns++;
    }
    if (strstr(line, ".\n"))
      break;
    align_flgs = data_align_flgs;
  }

  /* title lines, data within the table */

  if (borderflg)
    puts("<TABLE BORDER>");
  else
    puts("<TABLE>");

  tag = "TH";
  align_flgs = header_align_flgs;
  while (fgets(line, sizeof(line), fileptr)) {
    rip(line);
    if (!strncmp(line, ".TE", 3))
      break;
    if (!strcmp(line, "_"))
      continue;
    printf("<TR>");
    l = line;
    for (i = 0; i < columns; i++) {
      cp = strchr(l, tab);
      if (cp)
	*cp = 0;
      while (isspace(*l & 0xff))
	l++;
      if (*l) {
	for (l1 = l; *l1; l1++) ;
	while (--l1 >= l && isspace(*l1 & 0xff)) ;
	l1[1] = 0;
      }
      switch (align_flgs[i]) {
      case 'l':
	align_str = " ALIGN=\"left\"";
	break;
      case 'c':
	align_str = " ALIGN=\"center\"";
	break;
      case 'r':
	align_str = " ALIGN=\"right\"";
	break;
      default:
	align_str = "";
	break;
      }
      printf("<%s%s>%s</%s>",
	     tag,
	     align_str,
	     *l ? escape_special_characters(buf, l) : "&nbsp;",
	     tag);
      if (cp)
	l = cp + 1;
      else
	l = "";
    }
    puts("</TR>");
    tag = "TD";
    align_flgs = data_align_flgs;
  }
  puts("</TABLE>");

#else

  FILE *tempptr;
  char *cp;
  char buf[1024];
  char line[1024];
  char tempfilename[1024];

  tmpnam(tempfilename);
  sprintf(buf, "tbl -TX | nroff -mm | col -b | expand | sed '/^$/d' | sed '/^ *- [0-9]* -$/d' > %s", tempfilename);
  if (!(tempptr = popen(buf, "w"))) {
    perror("popen");
    exit(1);
  }
  fprintf(tempptr, ".TS\n");
  for (;;) {
    if (!fgets(line, sizeof(line), fileptr)) {
      error("table nesting error");
      fprintf(tempptr, ".TE\n");
      break;
    }
    linenum++;
    fprintf(tempptr, "%s", line);
    if (!strncmp(line, ".TE", 3))
      break;
  }
  if (pclose(tempptr)) {
    perror("pclose");
    exit(1);
  }
  if (!(tempptr = fopen(tempfilename, "r"))) {
    perror(tempfilename);
    exit(1);
  }
  puts("<PRE>");
  while (fgets(line, sizeof(line), tempptr)) {
    if ((cp = strchr(line, '\n')))
      *cp = 0;
    puts(escape_special_characters(buf, line));
  }
  puts("</PRE>");
  fclose(tempptr);
  if (remove(tempfilename))
    perror(tempfilename);

#endif

}

/*---------------------------------------------------------------------------*/

static void dot_VL(int argc, char **argv)
{
  start_list('D');
}

/*---------------------------------------------------------------------------*/

static void dot_ce(int argc, char **argv)
{
  if (argc != 1) {
    error("incorrect argument count");
    return;
  }
  centered = 1;
}

/*---------------------------------------------------------------------------*/

static void dot_nr(int argc, char **argv)
{
  if (argc != 3) {
    error("incorrect argument count");
    return;
  }
  if (strlen(argv[1]) != 2) {
    error("illegal register name");
    return;
  }
  setnumber(argv[1], atoi(argv[2]));
}

/*---------------------------------------------------------------------------*/

static void do_ignore(int argc, char **argv)
{
#if 0
  int i;

  printf("argc = %d\n", argc);
  for (i = 0; i < argc; i++)
    printf("argv[%d] = \"%s\"\n", i, argv[i]);
#endif
}

/*---------------------------------------------------------------------------*/

static void not_implemented(int argc, char **argv)
{
  error("not implemented");
}

/*---------------------------------------------------------------------------*/

struct commandtable {
  const char *name;
  void (*func) (int argc, char **argv);
};

static const struct commandtable pass1_commandtable[] =
{

  {".H", dot_H},
  {".nr", dot_nr},

  {0, do_ignore}
};

static const struct commandtable pass2_commandtable[] =
{

  {".AL", dot_AL},
  {".BL", dot_BL},
  {".DE", dot_DE},
  {".DS", dot_DS},
  {".H", dot_H},
  {".LE", dot_LE},
  {".LI", dot_LI},
  {".P", dot_P},
  {".PF", do_ignore},
  {".PH", do_ignore},
  {".S", do_ignore},
  {".SP", dot_SP},
  {".TC", do_ignore},
/*{".TE", do_ignore},*/
  {".TS", dot_TS},
  {".VL", dot_VL},
  {".\\\"", do_ignore},
  {".ce", dot_ce},
  {".ds", do_ignore},
  {".ft", do_ignore},
  {".nr", dot_nr},

  {0, not_implemented}
};

/*---------------------------------------------------------------------------*/

static void parse_command_line(char *line, const struct commandtable *cmdp)
{

  char *argv[256];
  char *b;
  char *l;
  char *t;
  char buf[1024];
  char tmp[1024];
  int argc;
  int quote;

  argc = 0;
  for (l = line, b = buf;;) {
    while (isspace(*l & 0xff))
      l++;
    if (!*l)
      break;
    t = tmp;
    if (*l == '"') {
      quote = *l++;
      while (*l && *l != quote)
	*t++ = *l++;
      if (*l)
	l++;
    } else {
      while (*l && !isspace(*l & 0xff)) {
	if (*l == '\\' && l[1] == ' ') {
	  *t++ = ' ';
	  l += 2;
	} else {
	  *t++ = *l++;
	}
      }
    }
    *t = 0;
    if (!strncmp(tmp, "\\\"", 2))
      break;
    if (argc)
      escape_special_characters(b, tmp);
    else
      strcpy(b, tmp);
    argv[argc++] = b;
    while (*b++) ;
  }
  argv[argc] = 0;
  if (argc) {
    for (;; cmdp++) {
      if (!cmdp->name || !strcmp(cmdp->name, *argv)) {
	cmdp->func(argc, argv);
	break;
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

static void reset_state(void)
{
  struct numbers *np;

  afterheader = 0;
  aftertitle = 0;
  centered = 0;
  headerlevel = 0;
  linenum = 0;
  listlevel = 0;

  while ((np = numbers)) {
    numbers = np->next;
    free(np);
  }

  setnumber("Cl", 2);
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char *cp;
  char buf[1024];
  char line[1024];

  if (argc != 2) {
    fprintf(stderr, "Usage: mm2html file\n");
    exit(1);
  }
  if (!(fileptr = fopen(argv[1], "r"))) {
    perror(argv[1]);
    exit(1);
  }

  reset_state();
  pass = 1;

  while (fgets(line, sizeof(line), fileptr)) {
    if ((cp = strchr(line, '\n')))
      *cp = 0;
    linenum++;
    if (*line == '.')
      parse_command_line(line, pass1_commandtable);
    else if (!*title) {
      escape_special_characters(title, line);
      while ((cp = strchr(title, '<')))
	strcpy(cp, strchr(cp, '>') + 1);
    }
  }

  rewind(fileptr);

  reset_state();
  pass = 2;

  puts("<HTML>");
  if (*title) {
    puts("<HEAD>");
    printf("<TITLE>%s</TITLE>\n", title);
    puts("</HEAD>");
  }
  puts("<BODY>");
  while (fgets(line, sizeof(line), fileptr)) {
    if ((cp = strchr(line, '\n')))
      *cp = 0;
    linenum++;
    if (*line == '.')
      parse_command_line(line, pass2_commandtable);
    else {
      if (!aftertitle) {
	printf("<H1>%s</H1>\n", title);
	aftertitle = 1;
	centered = 0;
      } else {
	puts(escape_special_characters(buf, line));
	if (centered) {
	  puts("<BR>");
	  centered = 0;
	}
      }
    }
  }
  puts("</BODY>");
  puts("</HTML>");

  if (listlevel)
    error("list nesting error");

  return 0;
}
