/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/ripcmd.c,v 1.5 1994-10-06 16:15:34 deyke Exp $ */

/* RIP-related user commands
 *   Al Broscious, N3FCT
 *   Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "cmdparse.h"
#include "timer.h"
#include "iface.h"
#include "udp.h"
#include "rip.h"
#include "commands.h"

struct cmds Ripcmds[] = {
	"accept",       dodroprefuse,   0,      2,
		"rip accept <gateway> ",
	"add",          doripadd,       0,      3,
		"rip add <dest> <interval> [<flags>]",
	"drop",         doripdrop,      0,      2,
		"rip drop <dest>",
	"merge",        doripmerge,     0,      0,      NULLCHAR,
	"refuse",       doaddrefuse,    0,      2,
		"rip refuse <gateway>",
	"request",      doripreq,       0,      2,      NULLCHAR,
	"status",       doripstat,      0,      0,      NULLCHAR,
	"trace",        doriptrace,     0,      0,      NULLCHAR,
	NULLCHAR,
};

int
dorip(
int argc,
char *argv[],
void *p)
{
	return subcmd(Ripcmds,argc,argv,p);
}

/* Add an entry to the RIP output list */
int
doripadd(
int argc,
char *argv[],
void *p)
{
	char flags = 0;

	if(argc > 3)
		flags = atoi(argv[3]);

	return rip_add(resolve(argv[1]),atol(argv[2]),flags);
}

/* Add an entry to the RIP refuse list */
int
doaddrefuse(
int argc,
char *argv[],
void *p)
{
	return riprefadd(resolve(argv[1]));
}

/* Drop an entry from the RIP output list */
int
doripdrop(
int argc,
char *argv[],
void *p)
{
	return rip_drop(resolve(argv[1]));
}

/* Drop an entry from the RIP refuse list */
int
dodroprefuse(
int argc,
char *argv[],
void *p)
{
	return riprefdrop(resolve(argv[1]));
}

/* Initialize the RIP listener */
int
doripinit(
int argc,
char *argv[],
void *p)
{
	return rip_init();
}
int
doripstop(
int argc,
char *argv[],
void *p)
{
	del_udp(Rip_cb);
	Rip_cb = NULLUDP;
	return 0;
}
int
doripreq(
int argc,
char *argv[],
void *p)
{
	uint16 replyport;

	if(argc > 2)
		replyport = atoi(argv[2]);
	else
		replyport = RIP_PORT;
	return ripreq(resolve(argv[1]),replyport);
}
/* Dump RIP statistics */
int
doripstat(
int argc,
char *argv[],
void *p)
{
	struct rip_list *rl;
	struct rip_refuse *rfl;

	printf("RIP: sent %lu rcvd %lu reqst %lu resp %lu unk %lu refused %lu\n",
	 Rip_stat.output, Rip_stat.rcvd, Rip_stat.request, Rip_stat.response,
	 Rip_stat.unknown,Rip_stat.refusals);
	if(Rip_list != NULLRL){
		printf("Active RIP output interfaces:\n");
		printf("Dest Addr       Interval Split\n");
		for(rl=Rip_list; rl != NULLRL; rl = rl->next){
			if(printf("%-16s%-9lu%-6u\n",
			 inet_ntoa(rl->dest),rl->interval,
			 !!(rl->flags&RIP_SPLIT)) == EOF)
				break;
		}
	}
	if(Rip_refuse != NULLREF){
		printf("Refusing announcements from gateways:\n");
		for(rfl=Rip_refuse; rfl != NULLREF;rfl = rfl->next){
			if(printf("%s\n",inet_ntoa(rfl->target)) == EOF)
				break;
		}
	}
	return 0;
}

int
doriptrace(
int argc,
char *argv[],
void *p)
{
	return setshort(&Rip_trace,"RIP tracing",argc,argv);
}
int
doripmerge(
int argc,
char *argv[],
void *p)
{
	return setbool(&Rip_merge,"RIP merging",argc,argv);
}
