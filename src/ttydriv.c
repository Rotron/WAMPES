/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/ttydriv.c,v 1.17 1993-04-02 14:26:19 deyke Exp $ */

/* TTY input line editing
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "tty.h"

#define LINESIZE        1024
#define LINEMAX         (LINESIZE-10)
#define RECALLSIZE      63

#define TT_UNKNOWN      0
#define TT_ANSI         1
#define TT_HP           2

#define KA_NONE         0       /* Use key literally */
#define KA_FK1          1       /* Function key 1 */
#define KA_FK2          2       /* Function key 2 */
#define KA_FK3          3       /* Function key 3 */
#define KA_FK4          4       /* Function key 4 */
#define KA_FK5          5       /* Function key 5 */
#define KA_FK6          6       /* Function key 6 */
#define KA_FK7          7       /* Function key 7 */
#define KA_FK8          8       /* Function key 8 */
#define KA_FK9          9       /* Function key 9 */
#define KA_FK10        10       /* Function key 10 */
#define KA_BACK        11       /* Delete previous character */
#define KA_CLEAR       12       /* Clear line */
#define KA_CLREOL      13       /* Clear to end of line */
#define KA_DELCHAR     14       /* Delete current character */
#define KA_DELWORD     15       /* Delete previous word */
#define KA_ECHO        16       /* Echo input to output */
#define KA_END         17       /* Go to end of line */
#define KA_HOME        18       /* Go to start of line */
#define KA_IGNORE      19       /* Ignore this key */
#define KA_LEFT        20       /* Go left one character */
#define KA_NEXT        21       /* Recall next line */
#define KA_PREV        22       /* Recall previous line */
#define KA_QUOTE       23       /* Quote next character */
#define KA_REDISPLAY   24       /* Redisplay line */
#define KA_RETURN      25       /* Return line to caller */
#define KA_RIGHT       26       /* Go right one character */
#define KA_SEARCH      27       /* Search line */
#define KA_TRANSMIT    28       /* Return line to caller (without CR/LF) */

struct keytable {
	const char str[8];
	char ttytype;
	char keyaction;
};

static struct keytable Keytable[] = {

	"\001",         TT_UNKNOWN,     KA_HOME,
	"\002",         TT_UNKNOWN,     KA_LEFT,
	"\004",         TT_UNKNOWN,     KA_DELCHAR,
	"\005",         TT_UNKNOWN,     KA_END,
	"\006",         TT_UNKNOWN,     KA_RIGHT,
	"\010",         TT_UNKNOWN,     KA_BACK,
	"\012",         TT_UNKNOWN,     KA_RETURN,
	"\013",         TT_UNKNOWN,     KA_CLREOL,
	"\014",         TT_UNKNOWN,     KA_REDISPLAY,
	"\015",         TT_UNKNOWN,     KA_RETURN,
	"\016",         TT_UNKNOWN,     KA_NEXT,
	"\020",         TT_UNKNOWN,     KA_PREV,
	"\022",         TT_UNKNOWN,     KA_SEARCH,
	"\024",         TT_UNKNOWN,     KA_TRANSMIT,
	"\025",         TT_UNKNOWN,     KA_CLEAR,
	"\026",         TT_UNKNOWN,     KA_QUOTE,
	"\027",         TT_UNKNOWN,     KA_DELWORD,
	"\030",         TT_UNKNOWN,     KA_CLEAR,
	"\033&r1L",     TT_HP,          KA_HOME,
	"\033&r1R",     TT_HP,          KA_END,
	"\033A",        TT_HP,          KA_PREV,
	"\033B",        TT_HP,          KA_NEXT,
	"\033C",        TT_HP,          KA_RIGHT,
	"\033D",        TT_HP,          KA_LEFT,
	"\033F",        TT_HP,          KA_END,
	"\033G",        TT_HP,          KA_CLREOL,
	"\033H",        TT_HP,          KA_HOME,
	"\033J",        TT_HP,          KA_CLEAR,
	"\033K",        TT_HP,          KA_CLREOL,
	"\033L",        TT_HP,          KA_IGNORE,
	"\033M",        TT_HP,          KA_CLEAR,
	"\033OA",       TT_ANSI,        KA_PREV,
	"\033OB",       TT_ANSI,        KA_NEXT,
	"\033OC",       TT_ANSI,        KA_RIGHT,
	"\033OD",       TT_ANSI,        KA_LEFT,
	"\033OP",       TT_ANSI,        KA_FK1,
	"\033OQ",       TT_ANSI,        KA_FK2,
	"\033OR",       TT_ANSI,        KA_FK3,
	"\033OS",       TT_ANSI,        KA_FK4,
	"\033OT",       TT_ANSI,        KA_FK5,
	"\033OU",       TT_ANSI,        KA_FK6,
	"\033OV",       TT_ANSI,        KA_FK7,
	"\033OW",       TT_ANSI,        KA_FK8,
	"\033OX",       TT_ANSI,        KA_FK9,
	"\033OY",       TT_ANSI,        KA_FK10,
	"\033Om",       TT_ANSI,        KA_FK8,
	"\033Ot",       TT_ANSI,        KA_FK9,
	"\033Ou",       TT_ANSI,        KA_FK10,
	"\033Ow",       TT_ANSI,        KA_FK5,
	"\033Ox",       TT_ANSI,        KA_FK6,
	"\033Oy",       TT_ANSI,        KA_FK7,
	"\033P",        TT_HP,          KA_DELCHAR,
	"\033Q",        TT_HP,          KA_IGNORE,
	"\033S",        TT_HP,          KA_ECHO,
	"\033T",        TT_HP,          KA_ECHO,
	"\033U",        TT_HP,          KA_NEXT,
	"\033V",        TT_HP,          KA_PREV,
	"\033Y",        TT_HP,          KA_END,
	"\033[11~",     TT_ANSI,        KA_FK1,
	"\033[12~",     TT_ANSI,        KA_FK2,
	"\033[13~",     TT_ANSI,        KA_FK3,
	"\033[14~",     TT_ANSI,        KA_FK4,
	"\033[15~",     TT_ANSI,        KA_FK5,
	"\033[17~",     TT_ANSI,        KA_FK6,
	"\033[18~",     TT_ANSI,        KA_FK7,
	"\033[19~",     TT_ANSI,        KA_FK8,
	"\033[5~",      TT_ANSI,        KA_PREV,
	"\033[6~",      TT_ANSI,        KA_NEXT,
	"\033[A",       TT_ANSI,        KA_PREV,
	"\033[B",       TT_ANSI,        KA_NEXT,
	"\033[C",       TT_ANSI,        KA_RIGHT,
	"\033[D",       TT_ANSI,        KA_LEFT,
	"\033[P",       TT_ANSI,        KA_FK1,
	"\033[Q",       TT_ANSI,        KA_FK2,
	"\033[R",       TT_ANSI,        KA_FK3,
	"\033[S",       TT_ANSI,        KA_FK4,
	"\033[T",       TT_ANSI,        KA_FK5,
	"\033[U",       TT_ANSI,        KA_FK6,
	"\033[V",       TT_ANSI,        KA_FK7,
	"\033[W",       TT_ANSI,        KA_FK8,
	"\033[X",       TT_ANSI,        KA_FK9,
	"\033[Y",       TT_ANSI,        KA_FK10,
	"\033[m",       TT_ANSI,        KA_FK8,
	"\033[t",       TT_ANSI,        KA_FK9,
	"\033[u",       TT_ANSI,        KA_FK10,
	"\033[w",       TT_ANSI,        KA_FK5,
	"\033[x",       TT_ANSI,        KA_FK6,
	"\033[y",       TT_ANSI,        KA_FK7,
	"\033h",        TT_HP,          KA_HOME,
	"\033p",        TT_HP,          KA_FK1,
	"\033q",        TT_HP,          KA_FK2,
	"\033r",        TT_HP,          KA_FK3,
	"\033s",        TT_HP,          KA_FK4,
	"\033t",        TT_HP,          KA_FK5,
	"\033u",        TT_HP,          KA_FK6,
	"\033v",        TT_HP,          KA_FK7,
	"\033w",        TT_HP,          KA_FK8,
	"\177",         TT_UNKNOWN,     KA_BACK,

	"",             TT_UNKNOWN,     KA_NONE

};

#define NEXT(i)         (((i) + 1) & RECALLSIZE)
#define PREV(i)         (((i) - 1) & RECALLSIZE)

char *Fkey_ptr;
char *Fkey_table[NUM_FKEY];

static int Ansiterminal = 1;
static int Rawmode;

static void printchr __ARGS((int chr));
static void backchr __ARGS((int chr));
static void delchr __ARGS((int chr));
static void inschr __ARGS((int chr));
static void clreol __ARGS((void));

/*---------------------------------------------------------------------------*/

int raw()
{
	Rawmode = 1;
	return 0;
}

/*---------------------------------------------------------------------------*/

int cooked()
{
	Rawmode = 0;
	return 0;
}

/*---------------------------------------------------------------------------*/

static void printchr(chr)
int chr;
{
	chr &= 0xff;
	if (chr < 32) {
		putchar('^');
		putchar(chr + 64);
	} else if (chr < 127) {
		putchar(chr);
	} else if (chr < 128) {
		putchar('^');
		putchar('?');
	} else if (chr < 159) {
		putchar('^');
		putchar(chr - 32);
	} else if (chr < 160) {
		putchar('^');
		putchar('>');
	} else if (chr < 255) {
		putchar(chr);
	} else {
		putchar('^');
		putchar('/');
	}
}

/*---------------------------------------------------------------------------*/

static void backchr(chr)
int chr;
{
	putchar('\b');
	chr &= 0x7f;
	if (chr < 32 || chr == 127)
		putchar('\b');
}

/*---------------------------------------------------------------------------*/

static void delchr(chr)
int chr;
{
	putchar('\033');
	if (Ansiterminal) putchar('[');
	putchar('P');
	chr &= 0x7f;
	if (chr < 32 || chr == 127) {
		putchar('\033');
		if (Ansiterminal) putchar('[');
		putchar('P');
	}
}

/*---------------------------------------------------------------------------*/

static void inschr(chr)
int chr;
{
	int c;

	if (Ansiterminal) {
		c = chr & 0x7f;
		printf("\033[%d@", (c < 32 || c == 127) ? 2 : 1);
		printchr(chr);
	} else {
		putchar('\033');
		putchar('Q');
		printchr(chr);
		putchar('\033');
		putchar('R');
	}
}

/*---------------------------------------------------------------------------*/

static void clreol()
{
	putchar('\033');
	if (Ansiterminal) putchar('[');
	putchar('K');
}

/*---------------------------------------------------------------------------*/

/* Accept characters from the incoming tty, buffer and process them
 * (if in cooked mode) or just pass them directly (if in raw mode).
 * Returns the number of characters available for use; if non-zero,
 * also stashes a pointer to the character(s) in the "buf" argument.
 */

int ttydriv(chr, buf)
int chr;
char **buf;
{

	static char linebuf[LINESIZE];
	static char *end = linebuf;
	static char *pos = linebuf;
	static char *recall_buffer[RECALLSIZE+1];
	static char keybuf[8];
	static int Escape_save;
	static int keycnt;
	static int quote;
	static int rptr, wptr;

	char *p, *f, *t;
	int cnt;
	int i;
	int keyaction;

	if (Rawmode) {
		*linebuf = chr;
		*buf = linebuf;
		return 1;
	}

	if (!chr) return 0;

	cnt = 0;
	keybuf[keycnt++] = chr;
	keyaction = KA_NONE;
	if (!quote) {
		for (i = 0; Keytable[i].str[0]; i++) {
			if (Keytable[i].str[0] == *keybuf &&
			    !strncmp(Keytable[i].str, keybuf, keycnt)) {
				if (strlen(Keytable[i].str) > keycnt)
					return 0;
				switch (Keytable[i].ttytype) {
				case TT_ANSI:
					Ansiterminal = 1;
					break;
				case TT_HP:
					Ansiterminal = 0;
					break;
				}
				keyaction = Keytable[i].keyaction;
				break;
			}
		}
	}

	if (quote) {
		delchr(quote);
		quote = 0;
		Escape = Escape_save;
	}

	switch (keyaction) {

	case KA_NONE:
		for (i = 0; i < keycnt; i++) {
			if (end - linebuf >= LINEMAX)
				putchar(7);
			else if (pos == end) {
				*end++ = keybuf[i];
				printchr(*pos++);
			} else {
				for (p = end - 1; p >= pos; p--) p[1] = *p;
				*pos++ = keybuf[i];
				end++;
				inschr(keybuf[i]);
			}
		}
		break;

	case KA_FK1:
	case KA_FK2:
	case KA_FK3:
	case KA_FK4:
	case KA_FK5:
	case KA_FK6:
	case KA_FK7:
	case KA_FK8:
	case KA_FK9:
	case KA_FK10:
		Fkey_ptr = Fkey_table[keyaction-KA_FK1];
		break;

	case KA_BACK:
		if (pos > linebuf) {
			backchr(*--pos);
			delchr(*pos);
			for (p = pos + 1; p < end; p++) p[-1] = *p;
			end--;
		}
		break;

	case KA_CLEAR:
		while (pos > linebuf) backchr(*--pos);
	case KA_CLREOL:
		if (end > pos) {
			end = pos;
			clreol();
		}
		break;

	case KA_DELCHAR:
		if (pos < end) {
			delchr(*pos);
			for (p = pos + 1; p < end; p++) p[-1] = *p;
			end--;
		}
		break;

	case KA_DELWORD:
		p = pos;
		while (p > linebuf && p[-1] == ' ') {
			backchr(*--p);
			delchr(*p);
		}
		while (p > linebuf && p[-1] != ' ') {
			backchr(*--p);
			delchr(*p);
		}
		for (f = pos, t = p; f < end; ) *t++ = *f++;
		pos = p;
		end = t;
		break;

	case KA_ECHO:
		for (i = 0; i < keycnt; i++) putchar(keybuf[i]);
		break;

	case KA_END:
		while (pos < end) printchr(*pos++);
		break;

	case KA_HOME:
		while (pos > linebuf) backchr(*--pos);
		break;

	case KA_IGNORE:
		break;

	case KA_LEFT:
		if (pos > linebuf) backchr(*--pos);
		break;

	case KA_NEXT:
		if (recall_buffer[rptr] && recall_buffer[NEXT(rptr)]) {
			if (end > linebuf) {
				while (pos > linebuf) backchr(*--pos);
				clreol();
			}
			rptr = NEXT(rptr);
			strcpy(linebuf, recall_buffer[rptr]);
			for (pos = linebuf; *pos; pos++) printchr(*pos);
			end = pos;
		} else
			putchar(7);
		break;

	case KA_PREV:
		if (recall_buffer[PREV(rptr)]) {
			if (end > linebuf) {
				while (pos > linebuf) backchr(*--pos);
				clreol();
			}
			rptr = PREV(rptr);
			strcpy(linebuf, recall_buffer[rptr]);
			for (pos = linebuf; *pos; pos++) printchr(*pos);
			end = pos;
		} else
			putchar(7);
		break;

	case KA_QUOTE:
		if (end - linebuf >= LINEMAX)
			putchar(7);
		else {
			Escape_save = Escape;
			Escape = 0;
			quote = '^';
			if (pos == end)
				printchr(quote);
			else
				inschr(quote);
			backchr(quote);
		}
		break;

	case KA_REDISPLAY:
		putchar('\n');
		for (p = linebuf; p < end; p++) printchr(*p);
		while (p > pos) backchr(*--p);
		break;

	case KA_RETURN:
	case KA_TRANSMIT:
		*end = 0;
		if (*linebuf == '\022') {
			rptr = wptr;
			while (recall_buffer[PREV(rptr)] &&
			       !strstr(recall_buffer[PREV(rptr)], linebuf + 1))
				rptr = PREV(rptr);
			if (recall_buffer[PREV(rptr)]) {
				while (pos > linebuf) backchr(*--pos);
				clreol();
				rptr = PREV(rptr);
				strcpy(linebuf, recall_buffer[rptr]);
				for (pos = linebuf; *pos; pos++) printchr(*pos);
				end = pos;
			} else {
				putchar(7);
				rptr = wptr;
			}
		} else {
			if (*linebuf &&
			    (!recall_buffer[PREV(wptr)] ||
			     strcmp(linebuf, recall_buffer[PREV(wptr)]))) {
				recall_buffer[wptr] = strdup(linebuf);
				wptr = NEXT(wptr);
				if (recall_buffer[wptr]) {
					free(recall_buffer[wptr]);
					recall_buffer[wptr] = 0;
				}
			}
			rptr = wptr;
			if (keyaction != KA_TRANSMIT) {
				*end++ = '\r';
				*end++ = '\n';
				*end   = 0;
				putchar('\n');
			}
			cnt = end - linebuf;
			pos = end = linebuf;
		}
		break;

	case KA_RIGHT:
		if (pos - linebuf >= LINEMAX)
			putchar(7);
		else {
			if (pos == end) *end++ = ' ';
			printchr(*pos++);
		}
		break;

	case KA_SEARCH:
		while (pos > linebuf) backchr(*--pos);
		if (end > pos) {
			end = pos;
			clreol();
		}
		*end++ = '\022';
		printchr(*pos++);
		break;

	}

	keycnt = 0;
	fflush(stdout);
	if (cnt) *buf = linebuf;
	return cnt;
}

