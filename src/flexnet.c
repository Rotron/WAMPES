/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/flexnet.c,v 1.7 1994-11-21 11:36:29 deyke Exp $ */

#include <stdio.h>

#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"
#include "lapb.h"
#include "trace.h"
#include "flexnet.h"
#include "cmdparse.h"
#include "commands.h"

#define DEFAULTDELAY    600     /* Default delay (1 minute) */
#define FLEXBUF         13      /* Size of flexnet call in ascii */
#define FLEXLEN         (AXALEN+1)      /* Length of flexnet call */
#define LENPOLL         201     /* Total length of poll packet */
#define LENROUT         256     /* Maximum length of rout packet */
#define MAXDELAY        3000    /* Maximum delay (5 minutes) */
#define MAXHOPCNT       30      /* Maximum allowed hop count */
#define MAXQCALLS       50      /* Maximum # of calls in qury packet */
#define POLLINTERVAL    (5*60*1000L)    /* Time between polls (5 minutes) */

#define FLEX_INIT       '0'     /* Link initialization */
#define FLEX_RPRT       '1'     /* Poll answer */
#define FLEX_POLL       '2'     /* Poll */
#define FLEX_ROUT       '3'     /* Routing information */
#define FLEX_QURY       '6'     /* Path query */
#define FLEX_RSLT       '7'     /* Query result */

enum e_token {
	NOTOKEN,                /* I do not have token */
	WANTTOKEN,              /* I do not have token, but did request it */
	HAVETOKEN               /* I have token */
};

struct peer {
	struct peer *next;      /* Linked-list pointer */
	char call[FLEXLEN];     /* Flexnet call */
	int remdelay;           /* Delay measured remotely (100ms steps) */
	int locdelay;           /* Delay measured locally (100ms steps) */
	double delay;           /* Smoothed delay (100ms units) */
	int32 lastpolltime;     /* Time of last poll (ms) */
	struct ax25_cb *axp;    /* AX.25 control block pointer */
	int id;                 /* Last AX.25 control block id */
	enum e_token token;     /* Token state */
	int permanent;          /* True if peer was created manually */
};

struct neighbor {
	struct neighbor *next;  /* Linked-list pointer */
	struct peer *peer;      /* Peer pointer */
	int delay;              /* Delay via this neighbor */
	int lastdelay;          /* Last delay reported to this neighbor */
};

struct dest {
	struct dest *next;      /* Linked-list pointer */
	char call[FLEXLEN];     /* Flexnet call */
	struct neighbor *neighbors;     /* List of neighbors */
};

struct querypkt {
	int hopcnt;             /* Hop count */
	int qsonum;             /* QSO number */
	char srccall[AXALEN];   /* Source call in AX.25 format */
	char destcall[AXALEN];  /* Destination call in AX.25 format */
	int numcalls;           /* Number of calls in bufs array */
	char bufs[MAXQCALLS][AXBUF];    /* Calls in ascii format */
};

static struct dest *Dests;      /* List of destinations */
static struct peer *Peers;      /* List of peers */
static struct timer Polltimer;  /* Poll timer */

/*---------------------------------------------------------------------------*/

#define iround(d)               ((int) ((d) + 0.5))

/*---------------------------------------------------------------------------*/

#define flexaddrcp(to, from)    memcpy(to, from, FLEXLEN)

/*---------------------------------------------------------------------------*/

static int flexsetcall(char *call, const char *ascii)
{
	if (setcall(call, (char *) ascii)) {
		printf("Invalid call \"%s\"\n", ascii);
		return (-1);
	}
	call[ALEN + 1] = call[ALEN];
	return 0;
}

/*---------------------------------------------------------------------------*/

static struct peer *find_peer(const char *call)
{
	struct peer *pp;

	for (pp = Peers; pp; pp = pp->next)
		if (addreq(pp->call, call))
			return pp;
	return 0;
}

/*---------------------------------------------------------------------------*/

static struct dest *find_dest(const char *call)
{
	struct dest *pd;

	for (pd = Dests; pd; pd = pd->next)
		if (addreq(pd->call, call))
			return pd;
	return 0;
}

/*---------------------------------------------------------------------------*/

static struct neighbor *find_neighbor(const struct dest *pd, const struct peer *pp)
{
	struct neighbor *pn;

	for (pn = pd->neighbors; pn; pn = pn->next)
		if (pn->peer == pp)
			return pn;
	return 0;
}

/*---------------------------------------------------------------------------*/

static struct neighbor *find_best_neighbor(const struct dest *pd)
{

	struct neighbor *pn;
	struct neighbor *pnbest = 0;

	for (pn = pd->neighbors; pn; pn = pn->next)
		if (pn->delay && (!pnbest || pnbest->delay > pn->delay))
			pnbest = pn;
	return pnbest;
}

/*---------------------------------------------------------------------------*/

static void update_axroute(const struct dest *pd)
{

	char call[AXALEN];
	int dest_is_peer;
	struct ax_route *rp;
	struct ax_route *rpd;
	struct ax_route *rpp;
	struct neighbor *pn;

	if (!(pn = find_best_neighbor(pd)))
		return;
	if (!(rpp = ax_routeptr(pn->peer->call, 0)))
		return;
	dest_is_peer = addreq(pd->call, pn->peer->call);
	for (addrcp(call, pd->call);; call[ALEN] += 2) {
		if (!(rpd = ax_routeptr(call, 1)))
			return;
		for (rp = rpp; rp && rp != rpd; rp = rp->digi) ;
		if (!rp && !rpd->perm) {
			if (dest_is_peer) {
				rpd->digi = rpp->digi;
				rpd->ifp = rpp->ifp;
			} else {
				rpd->digi = rpp;
				rpd->ifp = 0;
			}
			for (; rpd; rpd = rpd->digi)
				rpd->time = secclock();
		}
		if ((call[ALEN] & SSID) >= (pd->call[ALEN + 1] & SSID))
			break;
	}
}

/*---------------------------------------------------------------------------*/

static void delete_unreachables(void)
{

	struct dest **ppd;
	struct dest *pd;
	struct neighbor **ppn;
	struct neighbor *pn;

	for (ppd = &Dests; (pd = *ppd);) {
		for (ppn = &pd->neighbors; (pn = *ppn);) {
			if (!pn->delay && !pn->lastdelay) {
				*ppn = pn->next;
				free(pn);
			} else {
				ppn = &pn->next;
			}
		}
		if (!pd->neighbors) {
			*ppd = pd->next;
			free(pd);
		} else {
			ppd = &pd->next;
		}
	}
}

/*---------------------------------------------------------------------------*/

static void update_delay(const char *destcall, struct peer *pp, int delay)
{

	struct dest *pd;
	struct neighbor *pn;

	if (!(pd = find_dest(destcall))) {
		if (!delay)
			return;
		if (!(pd = (struct dest *) calloc(1, sizeof(struct dest))))
			return;
		pd->next = Dests;
		Dests = pd;
		flexaddrcp(pd->call, destcall);
	}
	if (!(pn = find_neighbor(pd, pp))) {
		if (!delay)
			return;
		if (!(pn = (struct neighbor *) calloc(1, sizeof(struct neighbor))))
			return;
		pn->next = pd->neighbors;
		pd->neighbors = pn;
		pn->peer = pp;
	}
	pn->delay = delay;
}

/*---------------------------------------------------------------------------*/

static void send_init(const struct peer *pp)
{

	char *cp;
	struct mbuf *bp;

	if ((bp = alloc_mbuf(6))) {
		cp = bp->data;
		*cp++ = FLEX_INIT;
		*cp++ = '0' + ((pp->axp->hdr.source[ALEN] & SSID) >> 1);
		*cp++ = ' ';
		*cp++ = ' ';
		*cp++ = ' ' + 1;
		*cp++ = '\r';
		bp->cnt = 6;
		send_ax25(pp->axp, bp, PID_FLEXNET);
	}
}

/*---------------------------------------------------------------------------*/

static void send_poll(struct peer *pp)
{

	char *cp;
	int i;
	struct mbuf *bp;

	if ((bp = alloc_mbuf(LENPOLL))) {
		pp->lastpolltime = msclock();
		cp = bp->data;
		*cp++ = FLEX_POLL;
		for (i = 0; i < LENPOLL - 2; i++)
			*cp++ = ' ';
		*cp++ = '\r';
		bp->cnt = LENPOLL;
		send_ax25(pp->axp, bp, PID_FLEXNET);
	}
}

/*---------------------------------------------------------------------------*/

static void clear_all_via_peer(struct peer *pp, int including_peer)
{

	struct dest *pd;
	struct neighbor *pn;

	for (pd = Dests; pd; pd = pd->next)
		if ((including_peer || !addreq(pd->call, pp->call)) &&
		    (pn = find_neighbor(pd, pp)))
			pn->delay = pn->lastdelay = 0;
}

/*---------------------------------------------------------------------------*/

static struct ax25_cb *setaxp(struct peer *pp)
{
	struct ax25 hdr;

	if (!(pp->axp = find_ax25(pp->call))) {
		memset((char *) &hdr, 0, sizeof(struct ax25));
		addrcp(hdr.dest, pp->call);
		if (!(pp->axp = open_ax25(&hdr, AX_ACTIVE, 0, 0, 0, 0)))
			return 0;
	}
	if (pp->id != pp->axp->id) {
		pp->id = pp->axp->id;
		pp->token = memcmp(pp->axp->hdr.dest, pp->axp->hdr.source, AXALEN) < 0 ?
			NOTOKEN : HAVETOKEN;
		clear_all_via_peer(pp, 0);
		send_init(pp);
		send_poll(pp);
	}
	return pp->axp;
}

/*---------------------------------------------------------------------------*/

static void send_rout(struct peer *pp)
{

	char *cp = 0;
	int delay;
	int i;
	int lastdelay;
	struct dest *pd;
	struct mbuf *bp = 0;
	struct neighbor *pnbest;
	struct neighbor *pnpeer;

	if (!setaxp(pp))
		return;
	for (pd = Dests; pd; pd = pd->next) {
		if (addreq(pd->call, pp->call))
			continue;
		if (!(pnpeer = find_neighbor(pd, pp))) {
			if (!(pnpeer = (struct neighbor *) calloc(1, sizeof(struct neighbor))))
				break;
			pnpeer->next = pd->neighbors;
			pd->neighbors = pnpeer;
			pnpeer->peer = pp;
		}
		delay = MAXDELAY + 1;
		pnbest = find_best_neighbor(pd);
		if (pnbest && pnbest != pnpeer)
			delay = pnbest->delay;
		if (!(lastdelay = pnpeer->lastdelay))
			lastdelay = MAXDELAY + 1;
		if (delay > lastdelay || iround(1.25 * delay) < lastdelay) {
#if 0
			if (pp->token != HAVETOKEN) {
				if (pp->token != WANTTOKEN &&
				    (bp = alloc_mbuf(3))) {
					cp = bp->data;
					*cp++ = FLEX_ROUT;
					*cp++ = '+';
					*cp++ = '\r';
					bp->cnt = 3;
					send_ax25(pp->axp, bp, PID_FLEXNET);
					pp->token = WANTTOKEN;
				}
				return;
			}
#endif
			if (bp && (cp - bp->data) > LENROUT - 14) {
				*cp++ = '\r';
				bp->cnt = cp - bp->data;
				send_ax25(pp->axp, bp, PID_FLEXNET);
				bp = 0;
			}
			if (!bp) {
				if (!(bp = alloc_mbuf(LENROUT)))
					return;
				cp = bp->data;
				*cp++ = FLEX_ROUT;
			}
			for (i = 0; i < ALEN; i++)
				*cp++ = (pd->call[i] >> 1) & 0x7f;
			*cp++ = '0' + ((pd->call[ALEN] & SSID) >> 1);
			*cp++ = '0' + ((pd->call[ALEN + 1] & SSID) >> 1);
			if (delay > MAXDELAY)
				delay = 0;
			sprintf(cp, "%d ", pnpeer->lastdelay = delay);
			while (*cp)
				cp++;
		}
	}
	if (bp) {
		*cp++ = '\r';
		bp->cnt = cp - bp->data;
		send_ax25(pp->axp, bp, PID_FLEXNET);
	}
}

/*---------------------------------------------------------------------------*/

static char *sprintflexcall(char *buf, const char *call)
{

	char *cp;
	int chr;
	int i;
	int max_ssid;
	int min_ssid;

	cp = buf;
	for (i = 0; i < ALEN; i++) {
		chr = (*call++ >> 1) & 0x7f;
		if (chr == ' ')
			break;
		*cp++ = chr;
	}
	min_ssid = (*call++ & SSID) >> 1;
	max_ssid = (*call & SSID) >> 1;
	if (min_ssid || min_ssid != max_ssid) {
		if (min_ssid == max_ssid)
			sprintf(cp, "-%d", min_ssid);
		else
			sprintf(cp, "-%d-%d", min_ssid, max_ssid);
	} else
		*cp = 0;
	return buf;
}

/*---------------------------------------------------------------------------*/

static void process_changes(void)
{

	struct dest *pd;
	struct peer *pp;

	for (pp = Peers; pp; pp = pp->next)
		send_rout(pp);
	delete_unreachables();
	for (pd = Dests; pd; pd = pd->next)
		update_axroute(pd);
}

/*---------------------------------------------------------------------------*/

static void delete_peer(struct peer *peer)
{

	struct dest *pd;
	struct neighbor **ppn;
	struct neighbor *pn;
	struct peer **ppp;
	struct peer *pp;

	for (ppp = &Peers; (pp = *ppp);) {
		if (pp == peer) {
			for (pd = Dests; pd; pd = pd->next) {
				for (ppn = &pd->neighbors; (pn = *ppn);) {
					if (pn->peer == pp) {
						*ppn = pn->next;
						free(pn);
						break;
					} else {
						ppn = &pn->next;
					}
				}
			}
			if ((pp->axp = find_ax25(pp->call)))
				disc_ax25(pp->axp);
			*ppp = pp->next;
			free(pp);
			if (!Peers)
				stop_timer(&Polltimer);
			return;
		} else {
			ppp = &pp->next;
		}
	}
}

/*---------------------------------------------------------------------------*/

static void polltimer_expired(void *unused)
{

	struct peer *pp;
	struct peer *ppnext = 0;

	start_timer(&Polltimer);
	for (pp = Peers; pp; pp = ppnext) {
		ppnext = pp->next;
		if (pp->lastpolltime) {
			if (pp->permanent)
				clear_all_via_peer(pp, 1);
			else
				delete_peer(pp);
		}
	}
	for (pp = Peers; pp; pp = pp->next) {
		if (setaxp(pp) && !pp->lastpolltime)
			send_poll(pp);
	}
	process_changes();
}

/*---------------------------------------------------------------------------*/

static struct peer *create_peer(const char *call)
{
	struct peer *pp;

	if ((pp = (struct peer *) calloc(1, sizeof(struct peer)))) {
		if (!Peers) {
			Polltimer.func = polltimer_expired;
			Polltimer.arg = 0;
			set_timer(&Polltimer, POLLINTERVAL);
		}
		start_timer(&Polltimer);
		pp->next = Peers;
		Peers = pp;
		flexaddrcp(pp->call, call);
		setaxp(pp);
	}
	return pp;
}

/*---------------------------------------------------------------------------*/

static int doflexnetdest(int argc, char *argv[], void *p)
{

	char buf[FLEXBUF];
	char call[FLEXLEN];
	int curr;
	int next;
	struct dest *pd1;
	struct dest *pd;
	struct neighbor *pn;

	if (argc > 1) {
		if (flexsetcall(call, argv[1]))
			return 1;
		if (!(pd1 = find_dest(call))) {
			printf("Destination \"%s\" not found\n", argv[1]);
			return 1;
		}
	} else
		pd1 = 0;
	printf("Call          Neighbors\n");
	for (pd = Dests; pd; pd = pd->next) {
		if (!pd1 || pd == pd1) {
			printf("%-12s", sprintflexcall(buf, pd->call));
			for (curr = 1; curr != (next = 0x7fffffff); curr = next)
				for (pn = pd->neighbors; pn; pn = pn->next)
					if (pn->delay == curr)
						printf("  %s (%d)", sprintflexcall(buf, pn->peer->call), pn->delay);
					else if (pn->delay > curr && pn->delay < next)
						next = pn->delay;
			putchar('\n');
		}
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetdestdebug(int argc, char *argv[], void *p)
{

	char buf[FLEXBUF];
	struct dest *pd;
	struct neighbor *pn;

	printf("Call          Neighbors\n");
	for (pd = Dests; pd; pd = pd->next) {
		printf("%-12s", sprintflexcall(buf, pd->call));
		for (pn = pd->neighbors; pn; pn = pn->next)
			printf("  %s (D=%d L=%d)", sprintflexcall(buf, pn->peer->call), pn->delay, pn->lastdelay);
		putchar('\n');
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinkadd(int argc, char *argv[], void *p)
{

	char call[FLEXLEN];
	struct peer *pp;

	if (flexsetcall(call, argv[1]))
		return 1;
	if (ismyax25addr(call)) {
		printf("Cannot add link to myself\n");
		return 1;
	}
	if (!(pp = find_peer(call)) && !(pp = create_peer(call))) {
		printf(Nospace);
		return 1;
	}
	pp->permanent = 1;
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinkdel(int argc, char *argv[], void *p)
{

	char call[FLEXLEN];
	struct peer *pp;

	if (flexsetcall(call, argv[1]))
		return 1;
	if (!(pp = find_peer(call))) {
		printf("Link \"%s\" not found\n", argv[1]);
		return 1;
	}
	delete_peer(pp);
	process_changes();
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinklist(int argc, char *argv[], void *p)
{

	char buf[FLEXBUF];
	int state;
	static const char tokenstr[] = "NWY";
	struct ax25_cb *axp;
	struct peer *pp;

	printf("Call         Remote  Local Smooth P T State\n");
	for (pp = Peers; pp; pp = pp->next) {
		state = (axp = find_ax25(pp->call)) ? axp->state : LAPB_DISCONNECTED;
		printf("%-12s %6d %6d %6d %c %c %s\n",
		       sprintflexcall(buf, pp->call),
		       pp->remdelay,
		       pp->locdelay,
		       iround(pp->delay),
		       pp->permanent ? 'P' : ' ',
		       tokenstr[pp->token],
		       Ax25states[state]);
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlink(int argc, char *argv[], void *p)
{

	static struct cmds Flexlinkcmds[] = {
		{ "add",    doflexnetlinkadd,  0, 2, "flexnet link add <call>" },
		{ "delete", doflexnetlinkdel,  0, 2, "flexnet link delete <call>" },
		{ 0,        0,                 0, 0, 0 }
	};

	if (argc < 2)
		return doflexnetlinklist(argc, argv, p);
	return subcmd(Flexlinkcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int addrmatch(const char *axcall, const char *flexcall)
{
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	return ((*axcall & SSID) >= (flexcall[0] & SSID) &&
		(*axcall & SSID) <= (flexcall[1] & SSID));
}

/*---------------------------------------------------------------------------*/

static int decode_query_packet(struct mbuf *bp, struct querypkt *qp)
{

	char *cp;
	int chr;
	int gotsrc;
	int i;

	if ((chr = PULLCHAR(&bp)) == -1)
		goto discard;
	qp->hopcnt = chr - ' ';
	if (qp->hopcnt < 0 || qp->hopcnt > MAXHOPCNT)
		goto discard;
	qp->qsonum = 0;
	for (i = 0; i < 5; i++) {
		chr = PULLCHAR(&bp);
		if (chr == ' ')
			continue;
		if (chr < '0' || chr > '9')
			goto discard;
		qp->qsonum = qp->qsonum * 10 + chr - '0';
	}
	gotsrc = 0;
	qp->numcalls = 0;
	cp = qp->bufs[qp->numcalls];
	for (;;) {
		chr = PULLCHAR(&bp);
		if ((chr == -1 || chr == ' ' || chr == '\r') && cp != qp->bufs[qp->numcalls]) {
			*cp = 0;
			if (strcmp(qp->bufs[qp->numcalls], "???") &&
			    setcall(qp->destcall, qp->bufs[qp->numcalls]))
				goto discard;
			qp->numcalls++;
			if (!gotsrc) {
				addrcp(qp->srccall, qp->destcall);
				gotsrc = 1;
			}
			cp = qp->bufs[qp->numcalls];
		}
		if (chr == ' ') {
			continue;
		} else if (chr == -1 || chr == '\r') {
			break;
		} else if ((chr >= '0' && chr <= '9') ||
			   (chr >= 'A' && chr <= 'Z') ||
			   (chr >= 'a' && chr <= 'z') ||
			   (chr == '-') ||
			   (chr == '?')) {
			if (qp->numcalls >= MAXQCALLS)
				goto discard;
			*cp++ = chr;
			if (cp - qp->bufs[qp->numcalls] >= AXBUF)
				goto discard;
		} else {
			goto discard;
		}
	}
	free_p(bp);
	return 0;

      discard:
	free_p(bp);
	return (-1);
}

/*---------------------------------------------------------------------------*/

static void print_query_packet(const struct querypkt *qp)
{
	int i;

	printf("Flexnet routing:");
	for (i = 0; i < qp->numcalls; i++)
		printf(" %s", qp->bufs[i]);
	putchar('\n');
}

/*---------------------------------------------------------------------------*/

static int send_query_packet(int type, const struct querypkt *qp, const char *call)
{

	char *cp;
	int i;
	struct dest *pd;
	struct mbuf *bp;
	struct neighbor *pn;

	for (pd = Dests; pd; pd = pd->next)
		if (addrmatch(call, pd->call))
			break;
	if (!pd)
		return (-1);
	if (!(pn = find_best_neighbor(pd)))
		return (-1);
	if (!setaxp(pn->peer))
		return (-1);
	if (!(bp = alloc_mbuf(LENROUT)))
		return (-1);
	cp = bp->data;
	*cp++ = type;
	*cp++ = ' ' + qp->hopcnt;
	sprintf(cp, "%5d", qp->qsonum);
	cp += 5;
	for (i = 0; i < qp->numcalls; i++) {
		if (cp - bp->data > LENROUT - 11) {
			free_p(bp);
			return (-1);
		}
		if (i > 0)
			*cp++ = ' ';
		strcpy(cp, qp->bufs[i]);
		while (*cp)
			cp++;
	}
	*cp++ = '\r';
	bp->cnt = cp - bp->data;
	send_ax25(pn->peer->axp, bp, PID_FLEXNET);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetquery(int argc, char *argv[], void *p)
{

	char *cp;
	char buf[AXBUF];
	int n;
	struct ax_route *rp;
	struct ax_route *rp_stack[20];
	struct dest *pd;
	struct iface *ifp;
	struct neighbor *pn;
	struct querypkt querypkt;

	memset((char *) &querypkt, 0, sizeof(struct querypkt));
	if (setcall(querypkt.destcall, argv[1])) {
		printf("Invalid call \"%s\"\n", argv[1]);
		return 1;
	}
	for (pd = Dests; pd; pd = pd->next)
		if (addrmatch(querypkt.destcall, pd->call))
			break;
	if (!pd) {
		printf("Destination \"%s\" not found\n", argv[1]);
		return 1;
	}
	if (!(pn = find_best_neighbor(pd))) {
		printf("Destination \"%s\" not reachable\n", argv[1]);
		return 1;
	}
	if (!(rp = ax_routeptr(pn->peer->call, 0))) {
		printf("No AX.25 route for link \"%s\"\n", pax25(buf, pn->peer->call));
		return 1;
	}
	ifp = 0;
	for (n = 0; rp; rp = rp->digi) {
		rp_stack[n++] = rp;
		ifp = rp->ifp;
	}
	addrcp(querypkt.srccall, ifp ? ifp->hwaddr : Mycall);
	pax25(querypkt.bufs[0], querypkt.srccall);
	querypkt.numcalls = 1;
	while (--n >= 0) {
		if (querypkt.numcalls >= MAXQCALLS) {
			printf("Too many hops\n");
			return 1;
		}
		cp = querypkt.bufs[querypkt.numcalls++];
		pax25(cp, rp_stack[n]->target);
		if (n > 0)
			for (; (*cp = Xtolower(*cp)); cp++) ;
	}
	if (addreq(pd->call, pn->peer->call)) {
		print_query_packet(&querypkt);
		return 0;
	}
	if (querypkt.numcalls >= MAXQCALLS) {
		printf("Too many hops\n");
		return 1;
	}
	pax25(querypkt.bufs[querypkt.numcalls++], querypkt.destcall);
	if (send_query_packet(FLEX_QURY, &querypkt, pn->peer->call)) {
		printf("Could not send query to %s\n", pax25(buf, pn->peer->call));
		return 1;
	}
	printf("Query sent to %s\n", pax25(buf, pn->peer->call));
	return 0;
}

/*---------------------------------------------------------------------------*/

int doflexnet(int argc, char *argv[], void *p)
{

	static struct cmds Flexnetcmds[] = {
		{ "dest",      doflexnetdest,      0, 0, 0 },
		{ "destdebug", doflexnetdestdebug, 0, 0, 0 },
		{ "link",      doflexnetlink,      0, 0, 0 },
		{ "query",     doflexnetquery,     0, 2, "flexnet query <call>" },
		{ 0,           0,                  0, 0, 0 }
	};

	return subcmd(Flexnetcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int pullnumber(struct mbuf **bpp, int *val)
{
	int chr;

	*val = 0;
	for (;;) {
		chr = PULLCHAR(bpp);
		if (chr < '0' || chr > '9')
			return chr;
		*val = *val * 10 + chr - '0';
	}
}

/*---------------------------------------------------------------------------*/

static int pullflexcall(struct mbuf **bpp, int chr, char *call)
{
	int i;

	for (i = 0; i < ALEN; i++) {
		if (chr == -1) {
			chr = PULLCHAR(bpp);
			if (chr == -1)
				return (-1);
		}
		*call++ = chr << 1;
		chr = -1;
	}
	for (i = 0; i < 2; i++) {
		chr = PULLCHAR(bpp);
		if (chr == -1)
			return (-1);
		*call++ = ((chr << 1) & SSID) | 0x60;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static void recv_init(struct peer *pp, struct mbuf *bp)
{
	int chr;

	if ((chr = PULLCHAR(&bp)) != -1)
		pp->call[ALEN + 1] = ((chr << 1) & SSID) | 0x60;
	free_p(bp);
	pp->token = memcmp(pp->axp->hdr.dest, pp->axp->hdr.source, AXALEN) < 0 ?
		NOTOKEN : HAVETOKEN;
	clear_all_via_peer(pp, 0);
}

/*---------------------------------------------------------------------------*/

static void recv_rprt(struct peer *pp, struct mbuf *bp)
{

	int delay;
	int olddelay;
	struct dest *pd;
	struct neighbor *pn;

	if (pullnumber(&bp, &delay) == -1)
		return;
	free_p(bp);
	olddelay = iround(pp->delay);
	if (delay > 0 && delay != DEFAULTDELAY) {
		pp->remdelay = delay;
		if (pp->delay)
			pp->delay = (7.0 * pp->delay + pp->remdelay) / 8.0;
		else
			pp->delay = pp->remdelay;
	}
	if (pp->lastpolltime) {
		pp->locdelay = iround((msclock() - pp->lastpolltime) / 200.0);
		if (pp->locdelay < 1)
			pp->locdelay = 1;
		pp->lastpolltime = 0;
		if (pp->delay)
			pp->delay = (7.0 * pp->delay + pp->locdelay) / 8.0;
		else
			pp->delay = pp->locdelay;
	}
	delay = iround(pp->delay);
	if (delay != olddelay) {
		for (pd = Dests; pd; pd = pd->next)
			for (pn = pd->neighbors; pn; pn = pn->next)
				if (pn->peer == pp && pn->delay)
					pn->delay = pn->delay - olddelay + delay;
	}
	update_delay(pp->call, pp, delay);
}

/*---------------------------------------------------------------------------*/

static void recv_poll(const struct peer *pp, struct mbuf *bp)
{
	free_p(bp);
	if ((bp = alloc_mbuf(20))) {
		sprintf(bp->data, "%c%d\r", FLEX_RPRT, pp->locdelay ? pp->locdelay : DEFAULTDELAY);
		bp->cnt = strlen(bp->data);
		send_ax25(pp->axp, bp, PID_FLEXNET);
	}
}

/*---------------------------------------------------------------------------*/

static void recv_rout(struct peer *pp, struct mbuf *bp)
{

	char *cp;
	char call[FLEXLEN];
	int chr;
	int delay;
	struct mbuf *bp2;

	for (;;) {
		chr = PULLCHAR(&bp);
		if ((chr >= '0' && chr <= '9') || (chr >= 'A' && chr <= 'Z')) {
			if (pullflexcall(&bp, chr, call))
				break;
			if (pullnumber(&bp, &delay) == -1)
				break;
			if (!ismyax25addr(call)) {
				if (delay)
					update_delay(call, pp, iround(1.125 * delay) + iround(pp->delay));
				else
					update_delay(call, pp, 0);
			}
		} else if (chr == '+') {
			if ((bp2 = alloc_mbuf(3))) {
				cp = bp2->data;
				*cp++ = FLEX_ROUT;
				*cp++ = '-';
				*cp++ = '\r';
				bp2->cnt = 3;
				send_ax25(pp->axp, bp2, PID_FLEXNET);
				pp->token = NOTOKEN;
			}
			break;
		} else if (chr == '-') {
			pp->token = HAVETOKEN;
			break;
		} else {
			break;
		}
	}
	free_p(bp);
}

/*---------------------------------------------------------------------------*/

static void recv_qury(struct peer *pp, struct mbuf *bp)
{

	char *cp;
	int n;
	struct ax_route *rp;
	struct ax_route *rp_stack[20];
	struct dest *pd;
	struct neighbor *pn;
	struct querypkt querypkt;

	if (decode_query_packet(bp, &querypkt))
		return;
	querypkt.hopcnt++;
	if (ismyax25addr(querypkt.destcall)) {
		send_query_packet(FLEX_RSLT, &querypkt, querypkt.srccall);
		return;
	}
	querypkt.numcalls--;
	for (pd = Dests; pd; pd = pd->next)
		if (addrmatch(querypkt.destcall, pd->call))
			break;
	if (pd &&
	    (pn = find_best_neighbor(pd)) &&
	    (rp = ax_routeptr(pn->peer->call, 0))) {
		for (n = 0; rp; rp = rp->digi)
			rp_stack[n++] = rp;
		while (--n >= 0) {
			if (querypkt.numcalls >= MAXQCALLS)
				return;
			cp = querypkt.bufs[querypkt.numcalls++];
			pax25(cp, rp_stack[n]->target);
			if (n > 0)
				for (; (*cp = Xtolower(*cp)); cp++) ;
		}
		if (addreq(pd->call, pn->peer->call)) {
			send_query_packet(FLEX_RSLT, &querypkt, querypkt.srccall);
		} else {
			if (querypkt.numcalls >= MAXQCALLS)
				return;
			pax25(querypkt.bufs[querypkt.numcalls++], querypkt.destcall);
			send_query_packet(FLEX_QURY, &querypkt, pn->peer->call);
		}
	} else {
		if (querypkt.numcalls >= MAXQCALLS)
			return;
		strcpy(querypkt.bufs[querypkt.numcalls++], "???");
		if (querypkt.numcalls >= MAXQCALLS)
			return;
		pax25(querypkt.bufs[querypkt.numcalls++], querypkt.destcall);
		send_query_packet(FLEX_RSLT, &querypkt, querypkt.srccall);
	}
}

/*---------------------------------------------------------------------------*/

static void recv_rslt(struct peer *pp, struct mbuf *bp)
{
	struct querypkt querypkt;

	if (decode_query_packet(bp, &querypkt))
		return;
	querypkt.hopcnt++;
	if (ismyax25addr(querypkt.srccall))
		print_query_packet(&querypkt);
	else
		send_query_packet(FLEX_RSLT, &querypkt, querypkt.srccall);
}

/*---------------------------------------------------------------------------*/

void flexnet_input(struct iface *iface, struct ax25_cb *axp, char *src, char *destination, struct mbuf *bp, int mcast)
{

	char call[FLEXLEN];
	struct peer *pp;

	if (!axp || !bp || mcast)
		goto discard;
	if (!(pp = find_peer(src))) {
		if (ismyax25addr(src))
			goto discard;
		addrcp(call, src);
		call[ALEN + 1] = call[ALEN];
		if (!(pp = create_peer(call)))
			goto discard;
	} else if (pp->id != axp->id) {
		setaxp(pp);
	}
	switch (PULLCHAR(&bp)) {

	case FLEX_INIT:
		recv_init(pp, bp);
		break;

	case FLEX_RPRT:
		recv_rprt(pp, bp);
		break;

	case FLEX_POLL:
		recv_poll(pp, bp);
		break;

	case FLEX_ROUT:
		recv_rout(pp, bp);
		break;

	case FLEX_QURY:
		recv_qury(pp, bp);
		break;

	case FLEX_RSLT:
		recv_rslt(pp, bp);
		break;

	default:
		goto discard;

	}
	process_changes();
	return;

      discard:
	free_p(bp);
}

/*---------------------------------------------------------------------------*/

void flexnet_dump(FILE *fp, struct mbuf **bpp)
{

	char buf[FLEXBUF];
	char call[FLEXLEN];
	int chr;
	int func;
	int i;
	struct querypkt querypkt;

	if (bpp == NULLBUFP || *bpp == NULLBUF)
		return;
	fprintf(fp, "FLEXNET: len %d", len_p(*bpp));
	switch (func = PULLCHAR(bpp)) {

	case -1:
		goto too_short;

	case FLEX_INIT:
		fprintf(fp, " Link setup");
		if ((i = PULLCHAR(bpp)) == -1)
			goto too_short;
		fprintf(fp, " - Max SSID: %d", i & 0xf);
		break;

	case FLEX_RPRT:
		fprintf(fp, " Poll response");
		if (pullnumber(bpp, &i) == -1)
			goto too_short;
		fprintf(fp, " - Delay: %d", i);
		break;

	case FLEX_POLL:
		fprintf(fp, " Poll");
		break;

	case FLEX_ROUT:
		fprintf(fp, " Routing:");
		for (;;) {
			chr = PULLCHAR(bpp);
			if (chr == -1) {
				goto too_short;
			} else if ((chr >= '0' && chr <= '9') || (chr >= 'A' && chr <= 'Z')) {
				if (pullflexcall(bpp, chr, call))
					goto too_short;
				fprintf(fp, "\n  %-12s", sprintflexcall(buf, call));
				if (pullnumber(bpp, &i) == -1)
					goto too_short;
				if (!i)
					fprintf(fp, " Link down");
				else
					fprintf(fp, " Delay %d", i);
			} else if (chr == '+') {
				fprintf(fp, "\n  Request token");
			} else if (chr == '-') {
				fprintf(fp, "\n  Release token");
			} else if (chr == '\r') {
				break;
			} else {
				fprintf(fp, "\n  Unknown character '%c'", chr);
				break;
			}
		}
		break;

	case FLEX_QURY:
	case FLEX_RSLT:
		if (func == FLEX_QURY)
			fprintf(fp, " Route query");
		else
			fprintf(fp, " Query response");
		if (decode_query_packet(*bpp, &querypkt)) {
			*bpp = 0;
			fprintf(fp, " - bad packet\n");
			return;
		}
		*bpp = 0;
		fprintf(fp, " - hopcnt: %d qso: %d\n  ", querypkt.hopcnt, querypkt.qsonum);
		for (i = 0; i < querypkt.numcalls; i++)
			printf(" %s", querypkt.bufs[i]);
		break;

	default:
		fprintf(fp, " Unknown type: %d", func);
		break;

	}
	putc('\n', fp);
	return;

      too_short:
	fprintf(fp, " - packet too short\n");
}