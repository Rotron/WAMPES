/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/icmpcmd.c,v 1.16 1994-10-06 16:15:26 deyke Exp $ */

/* ICMP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <sys/time.h>
#include "global.h"
#include "icmp.h"
#include "ip.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "timer.h"
#include "socket.h"
#include "cmdparse.h"
#include "commands.h"

#define NULLPING (struct ping *)0
#define PMOD    7

/* ID fields for pings; indicates a oneshot or repeat ping */
#define ONESHOT 0
#define REPEAT  1

/* Hash table list heads */
struct ping *ping[PMOD];

static int doicmpec(int argc, char *argv[],void *p);
static int doicmpstat(int argc, char *argv[],void *p);
static int doicmptr(int argc, char *argv[],void *p);
static int pingem(int32 target,int seq,int id,int len);
static void ptimeout(void *p);
static uint16 hash_ping(int32 dest);
static struct ping *add_ping(int32 dest);
static void del_ping(struct ping *pp);

static struct cmds Icmpcmds[] = {
	"echo",         doicmpec,       0, 0, NULLCHAR,
	"status",       doicmpstat,     0, 0, NULLCHAR,
	"trace",        doicmptr,       0, 0, NULLCHAR,
	NULLCHAR
};

int Icmp_trace;
static int Icmp_echo = 1;

int
doicmp(
int argc,
char *argv[],
void *p)
{
	return subcmd(Icmpcmds,argc,argv,p);
}

static int
doicmpstat(
int argc,
char *argv[],
void *p)
{
	register int i;
	int lim;

	/* Note that the ICMP variables are shown in column order, because
	 * that lines up the In and Out variables on the same line
	 */
	lim = NUMICMPMIB/2;
	for(i=1;i<=lim;i++){
		printf("(%2u)%-20s%10lu",i,Icmp_mib[i].name,
		 Icmp_mib[i].value.integer);
		printf("     (%2u)%-20s%10lu\n",i+lim,Icmp_mib[i+lim].name,
		 Icmp_mib[i+lim].value.integer);
	}
	return 0;
}
static int
doicmptr(
int argc,
char *argv[],
void *p)
{
	return setbool(&Icmp_trace,"ICMP tracing",argc,argv);
}
static int
doicmpec(
int argc,
char *argv[],
void *p)
{
	return setbool(&Icmp_echo,"ICMP echo response accept",argc,argv);
}

/* Send ICMP Echo Request packets */
int
doping(
int argc,
char *argv[],
void *p)
{
	int32 dest;
	struct ping *pp1;
	register struct ping *pp;
	uint16 hval;
	int i;
	uint16 len;

	if(argc < 2){
		printf("Host                Sent    Rcvd   %%   Srtt   Mdev  Length  Interval\n");
		for(i=0;i<PMOD;i++){
			for(pp = ping[i];pp != NULLPING;pp = pp->next){
				printf("%-16s",inet_ntoa(pp->target));
				printf("%8lu%8lu",pp->sent,pp->responses);
				printf("%4lu",
				 (long)pp->responses * 100 / pp->sent);
				printf("%7lu", pp->srtt);
				printf("%7lu", pp->mdev);
				printf("%8u", pp->len);
				printf("%10lu\n",
				 dur_timer(&pp->timer) / 1000);
			}
		}
		return 0;
	}
	if(strcmp(argv[1],"clear") == 0){
		for(i=0;i<PMOD;i++){
			for(pp = ping[i];pp != NULLPING;pp = pp1){
				pp1 = pp->next;
				del_ping(pp);
			}
		}
		return 0;
	}
	if((dest = resolve(argv[1])) == 0){
		printf("Host %s unknown\n",argv[1]);
		return 1;
	}
	if(argc > 2) {
		len = atoi(argv[2]);
		if (len < ICMPLEN) {
			printf("packet size too small, minimum size is %d bytes\n", ICMPLEN);
			return 1;
		}
	} else
		len = 64;
	/* See if dest is already in table */
	hval = hash_ping(dest);
	for(pp = ping[hval]; pp != NULLPING; pp = pp->next){
		if(pp->target == dest){
			break;
		}
	}
	if(argc > 3){
		/* Inter-ping time is specified; set up timer structure */
		if(pp == NULLPING)
			pp = add_ping(dest);
		set_timer(&pp->timer, atoi(argv[3]) * 1000L);
		pp->timer.func = ptimeout;
		pp->timer.arg = pp;
		pp->target = dest;
		pp->len = len;
		start_timer(&pp->timer);
		pingem(dest,(uint16)pp->sent++,REPEAT,len);
	} else
		pingem(dest,0,ONESHOT,len);

	return 0;
}

/* Called by ping timeout */
static void
ptimeout(
void *p)
{
	register struct ping *pp;

	/* Send another ping */
	pp = (struct ping *)p;
	pingem(pp->target,(uint16)pp->sent++,REPEAT,pp->len);
	start_timer(&pp->timer);
}
void
echo_proc(
int32 source,
int32 dest,
struct icmp *icmp,
struct mbuf *bp)
{
	register struct ping *pp;
	uint16 hval;
	int32 rtt,abserr;
	struct timeval tv,timestamp;

	/* Get stamp */
	if(pullup(&bp,(char *)&timestamp,sizeof(timestamp))
	 != sizeof(timestamp)){
		/* The timestamp is missing! */
		rtt = -1;
	} else {
		gettimeofday(&tv, (struct timezone *) 0);
		rtt = (tv.tv_sec  - timestamp.tv_sec ) * 1000 +
		      (tv.tv_usec - timestamp.tv_usec) / 1000;
	}
	free_p(bp);

	hval = hash_ping(source);
	for(pp = ping[hval]; pp != NULLPING; pp = pp->next)
		if(pp->target == source)
			break;
	if(pp == NULLPING || icmp->args.echo.id != REPEAT){
		if(!Icmp_echo)
			return;
		printf(
		  (rtt == -1) ?
		    "%s: echo reply id %u seq %u\n" :
		    "%s: echo reply id %u seq %u, %lu ms\n",
		  inet_ntoa(source),
		  icmp->args.echo.id,icmp->args.echo.seq,
		  (long)rtt);
	} else {
		/* Repeated poll, just keep stats */
		pp->responses++;
		if (rtt == -1)
			return;
		if(pp->responses == 1){
			/* First response, base entire SRTT on it */
			pp->srtt = rtt;
		} else {
			abserr = rtt > pp->srtt ? rtt - pp->srtt : pp->srtt - rtt;
			pp->srtt = (7*pp->srtt + rtt + 4) >> 3;
			pp->mdev = (3*pp->mdev + abserr + 2) >> 2;
		}
	}
}
/* Send ICMP Echo Request packet */
static int
pingem(
int32 target,   /* Site to be pinged */
int seq,        /* ICMP Echo Request sequence number */
int id,         /* ICMP Echo Request ID */
int len)        /* Length of data field */
{
	struct mbuf *data;
	struct mbuf *bp;
	struct icmp icmp;
	int i;
	struct timeval tv;

	if ((data = alloc_mbuf(len)) == NULLBUF)
		return -1;
	for (i = ICMPLEN; i < len; i++)
		data->data[i] = i;
	data->data += ICMPLEN;
	data->cnt = len - ICMPLEN;
	/* Insert timestamp and build ICMP header */
	if (len >= ICMPLEN + sizeof(struct timeval)) {
		gettimeofday(&tv, (struct timezone *) 0);
		memcpy(data->data, (char *) &tv, sizeof(struct timeval));
	}
	icmpOutEchos++;
	icmpOutMsgs++;
	icmp.type = ICMP_ECHO;
	icmp.code = 0;
	icmp.args.echo.seq = seq;
	icmp.args.echo.id = id;
	if((bp = htonicmp(&icmp,data)) == NULLBUF){
		free_p(data);
		return 0;
	}
	return ip_send(INADDR_ANY,target,ICMP_PTCL,0,0,bp,len,0,0);
}
static uint16
hash_ping(
int32 dest)
{
	uint16 hval;

	hval = (hiword(dest) ^ loword(dest)) % PMOD;
	return hval;
}
/* Add entry to ping table */
static
struct ping *
add_ping(
int32 dest)
{
	struct ping *pp;
	uint16 hval;

	pp = (struct ping *)calloc(1,sizeof(struct ping));
	if(pp == NULLPING)
		return NULLPING;

	hval = hash_ping(dest);
	pp->prev = NULLPING;
	pp->next = ping[hval];
	if(pp->next != NULLPING)
		pp->next->prev = pp;
	ping[hval] = pp;
	return pp;
}
/* Delete entry from ping table */
static void
del_ping(
struct ping *pp)
{
	uint16 hval;

	stop_timer(&pp->timer);

	if(pp->next != NULLPING)
		pp->next->prev = pp->prev;
	if(pp->prev != NULLPING) {
		pp->prev->next = pp->next;
	} else {
		hval = hash_ping(pp->target);
		ping[hval] = pp->next;
	}
	free(pp);
}
