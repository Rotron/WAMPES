/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/trace.c,v 1.4 1990-09-11 13:46:41 deyke Exp $ */

#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "trace.h"
#include "pktdrvr.h"
#include "commands.h"
#include "hpux.h"

static void ascii_dump __ARGS((FILE *fp,struct mbuf **bpp));
static void ctohex __ARGS((char *buf,int c));
static void fmtline __ARGS((FILE *fp,int addr,char *buf,int len));
static void hex_dump __ARGS((FILE *fp,struct mbuf **bpp));
static void showtrace __ARGS((struct iface *ifp));

/* Redefined here so that programs calling dump in the library won't pull
 * in the rest of the package
 */
static char nospace[] = "No space!!\n";

void
dump(iface,direction,type,bp)
register struct iface *iface;
int direction;
unsigned type;
struct mbuf *bp;
{
	struct mbuf *tbp;
	void (*func) __ARGS((FILE *,struct mbuf **,int));
	int16 size;
	time_t timer;
	char *cp;

	if(iface == NULL || (iface->trace & direction) == 0)
		return; /* Nothing to trace */

	switch(direction){
	case IF_TRACE_IN:
		if((iface->trace & IF_TRACE_NOBC)
		 && (Tracef[type].addrtest != NULLFP)
		 && (*Tracef[type].addrtest)(iface,bp) == 0)
			return;         /* broadcasts are suppressed */
		cp = ctime(&currtime);
		cp[24] = '\0';
		fprintf(iface->trfp,"%s  - %s recv:\n",cp,iface->name);
		break;
	case IF_TRACE_OUT:
		cp = ctime(&currtime);
		cp[24] = '\0';
		fprintf(iface->trfp,"%s - %s sent:\n",cp,iface->name);
		break;
	}
	if(bp == NULLBUF || (size = len_p(bp)) == 0){
		fprintf(iface->trfp,"empty packet!!\n");
		return;
	}

	if(type < NCLASS)
		func = Tracef[type].tracef;
	else
		func = NULLVFP;

	dup_p(&tbp,bp,0,size);
	if(tbp == NULLBUF){
		fprintf(iface->trfp,nospace);
		return;
	}
	if(func != NULLVFP)
		(*func)(iface->trfp,&tbp,1);
	if(iface->trace & IF_TRACE_ASCII){
		/* Dump only data portion of packet in ascii */
		ascii_dump(iface->trfp,&tbp);
	} else if(iface->trace & IF_TRACE_HEX){
		/* Dump entire packet in hex/ascii */
		free_p(tbp);
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp != NULLBUF)
			hex_dump(iface->trfp,&tbp);
		else
			fprintf(iface->trfp,nospace);
	}
	free_p(tbp);
}

/* Dump an mbuf in hex */
static void
hex_dump(fp,bpp)
FILE *fp;
register struct mbuf **bpp;
{
	int16 n;
	int16 address;
	char buf[16];

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;

	address = 0;
	while((n = pullup(bpp,buf,sizeof(buf))) != 0){
		fmtline(fp,address,buf,n);
		address += n;
	}
}
/* Dump an mbuf in ascii */
static void
ascii_dump(fp,bpp)
FILE *fp;
register struct mbuf **bpp;
{
	int c;
	register int16 tot;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;

	tot = 0;
	while((c = PULLCHAR(bpp)) != -1){
		if((tot % 64) == 0)
			fprintf(fp,"%04x  ",tot);
		putc(isprint(uchar(c)) ? c : '.',fp);
		if((++tot % 64) == 0)
			fprintf(fp,"\n");
	}
	if((tot % 64) != 0)
		fprintf(fp,"\n");
}
/* Print a buffer up to 16 bytes long in formatted hex with ascii
 * translation, e.g.,
 * 0000: 30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f  0123456789:;<=>?
 */
static void
fmtline(fp,addr,buf,len)
FILE *fp;
int16 addr;
char *buf;
int16 len;
{
	char line[80];
	register char *aptr,*cptr;
	register char c;

	memset(line,' ',sizeof(line));
	ctohex(line,(int16)hibyte(addr));
	ctohex(line+2,(int16)lobyte(addr));
	aptr = &line[6];
	cptr = &line[55];
	while(len-- != 0){
		c = *buf++;
		ctohex(aptr,(int16)uchar(c));
		aptr += 3;
		c &= 0x7f;
		*cptr++ = isprint(uchar(c)) ? c : '.';
	}
	*cptr++ = '\n';
	fwrite(line,1,(unsigned)(cptr-line),fp);
}
/* Convert byte to two ascii-hex characters */
static void
ctohex(buf,c)
register char *buf;
register int16 c;
{
	static char hex[] = "0123456789abcdef";

	*buf++ = hex[hinibble(c)];
	*buf = hex[lonibble(c)];
}

/* Modify or displace interface trace flags */
int
dotrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;

	if(argc < 2){
		for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
			showtrace(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULLIF){
		tprintf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc >= 3)
		ifp->trace = htoi(argv[2]);

	/* Always default to stdout unless trace file is given */
	if(ifp->trfp != NULLFILE && ifp->trfp != stdout)
		fclose(ifp->trfp);
	ifp->trfp = stdout;
	if(ifp->trfile != NULLCHAR)
		free(ifp->trfile);
	ifp->trfile = NULLCHAR;

	if(argc >= 4){
		if((ifp->trfp = fopen(argv[3],APPEND_TEXT)) == NULLFILE){
			tprintf("Can't write to %s\n",argv[3]);
			ifp->trfp = stdout;
		} else {
			ifp->trfile = strdup(argv[3]);
		}
	}
	showtrace(ifp);
	return 0;
}
/* Display the trace flags for a particular interface */
static void
showtrace(ifp)
register struct iface *ifp;
{
	if(ifp == NULLIF)
		return;
	tprintf("%s:",ifp->name);
	if(ifp->trace & (IF_TRACE_IN | IF_TRACE_OUT)){
		if(ifp->trace & IF_TRACE_IN)
			tprintf(" input");
		if(ifp->trace & IF_TRACE_OUT)
			tprintf(" output");

		if(ifp->trace & IF_TRACE_NOBC)
			tprintf(" - no broadcasts");

		if(ifp->trace & IF_TRACE_HEX)
			tprintf(" (Hex/ASCII dump)");
		else if(ifp->trace & IF_TRACE_ASCII)
			tprintf(" (ASCII dump)");
		else
			tprintf(" (headers only)");
		if(ifp->trfile != NULLCHAR)
			tprintf(" trace file: %s",ifp->trfile);
		tprintf("\n");
	} else
		tprintf(" tracing off\n");
}

