/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/trace.c,v 1.3 1990-08-23 17:34:24 deyke Exp $ */

#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "iface.h"
#include "trace.h"
#include "session.h"

static void ctohex();

/* Redefined here so that programs calling dump in the library won't pull
 * in the rest of the package
 */
static char nospace[] = "No space!!\n";
static char nullpak[] = "empty packet!!\n";
static char if_tr_i[] = "%s recv: [%s]\n";
static char if_tr_o[] = "%s sent: [%s]\n";

FILE *trfp = stdout;            /* file pointer used for tracing */
int trcount = 0;                /* used to close file for flushing */
char trname[40];                /* name if not stdout */
int notraceall;                 /* 0 = trace all, 1 = only in cmd mode */
extern int mode;                /* command mode or not */
extern long  currtime;

void
dump(interface,direction,type,bp)
register struct iface *interface;
int direction;
unsigned type;
struct mbuf *bp;
{
	struct mbuf *tbp;
	void ascii_dump(),hex_dump();
	void (*func) __ARGS((FILE *,struct mbuf **,int));
	char *cp;
	long t;
	int16 size;

	if((interface->trace & direction) == 0)
		return; /* Nothing to trace */

	if (notraceall && mode != CMD_MODE)
		return; /* No trace while in session, if mode */

	if(bp == NULLBUF || (size = len_p(bp)) == 0)
		return;

	cp = ctime(&currtime);
	rip(cp);

	switch(direction){
	case IF_TRACE_IN:
		fprintf(trfp,if_tr_i,interface->name,cp);
		break;
	case IF_TRACE_OUT:
		fprintf(trfp,if_tr_o,interface->name,cp);
		break;
	}
	if(bp == NULLBUF || (size = len_p(bp)) == 0){
		fprintf(trfp,nullpak);
		goto trdone;
	}
	if(type < NTRACE)
		func = tracef[type];
	else
		func = 0;

	dup_p(&tbp,bp,0,size);
	if(tbp == NULLBUF){
		fprintf(trfp,nospace);
		if (trfp != stdout)
		  printf(nospace);
		goto trdone;
	}
	if(func)
		(*func)(trfp,&tbp,1);
	if(interface->trace & IF_TRACE_ASCII){
		/* Dump only data portion of packet in ascii */
		ascii_dump(&tbp);
	} else if(interface->trace & IF_TRACE_HEX){
		/* Dump entire packet in hex/ascii */
		free_p(tbp);
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp != NULLBUF)
			hex_dump(&tbp);
		else {
			fprintf(trfp,nospace);
			if (trfp != stdout)
			  printf(nospace);
		}
	}
	free_p(tbp);

#ifdef SCREEN
	(void)outscreen(0);             /* switch back to main screen */
#endif

      trdone:
	if(trfp != stdout) {
#ifndef ATARI_ST
	  fflush(trfp);
#endif
#if (defined(MSDOS) || defined(ATARI_ST))
	  if (++trcount > 25) {
	    fclose(trfp);
	    trfp = fopen(trname,"a");
	    trcount = 0;
	  }
#endif
	  if (trfp == NULLFILE || ferror(trfp)) {
	    printf("Error on %s - trace to console\n",trname);
	    if (trfp != NULLFILE && trfp != stdout)
	      fclose(trfp);
	    trfp = stdout;
	  }
	}
	fflush(stdout);
}

/* Dump an mbuf in hex */
void
hex_dump(bpp)
register struct mbuf **bpp;
{
	int16 n;
	int16 address;
	void fmtline();
	char buf[16];

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;

	address = 0;
	while((n = pullup(bpp,buf,sizeof(buf))) != 0){
		fmtline(address,buf,n);
		address += n;
	}
}
/* Dump an mbuf in ascii */
void
ascii_dump(bpp)
register struct mbuf **bpp;
{
	char c;
	register int16 tot;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;

	tot = 0;
	while(pullup(bpp,&c,1) == 1){
		if((tot % 64) == 0) {
			fprintf(trfp,"%04x  ",tot);
		}
#ifdef PC9801
		if ((c >= 0x20) || (c < 0)) {
#else
		if(isprint(c)) {
#endif
			putc(c,trfp);
		} else {
			putc('.',trfp);
		}
		tot++;
		if((tot % 64) == 0) {
#ifdef PC9801
			fprintf(trfp," \n");
#else
			fprintf(trfp,"\n");
#endif
		}
	}
	if((tot % 64) != 0) {
#ifdef PC9801
		fprintf(trfp," \n");
#else
		fprintf(trfp,"\n");
#endif
	}
}

/* Print a buffer up to 16 bytes long in formatted hex with ascii
 * translation, e.g.,
 * 0000: 30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f  0123456789:;<=>?
 */
void
fmtline(addr,buf,len)
int16 addr;
char *buf;
int16 len;
{
	char line[80];
	register char *aptr,*cptr;
	register int16 c;

	memset(line,' ',sizeof(line));
	ctohex(line,(int16)hibyte(addr));
	ctohex(line+2,(int16)lobyte(addr));
	aptr = &line[6];
	cptr = &line[55];
	while(len-- != 0){
		c = *buf++ & 0xff;
		ctohex(aptr,c);
		aptr += 3;
		c &= 0x7f;
		*cptr++ = isprint(c) ? c : '.';
	}
#ifdef PC9801
	*cptr++ = ' ';
#endif
	*cptr++ = '\n';
	fwrite(line,1,(unsigned)(cptr-line),trfp);
	/* added */
}
/* Convert byte to two ascii-hex characters */
static
void
ctohex(buf,c)
register char *buf;
register int16 c;
{
	static char hex[] = "0123456789abcdef";

	buf[0] = hex[hinibble(c)];
	buf[1] = hex[lonibble(c)];
}

