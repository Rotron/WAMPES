/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/flexnet.c,v 1.1 1994-10-21 11:55:13 deyke Exp $ */

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

#define DEFAULTDELAY    600             /* Default delay (1 minute) */
#define FLEXBUF         13              /* Size of flexnet call in ascii */
#define FLEXLEN         (AXALEN+1)      /* Length of flexnet call */
#define LENPOLL         201             /* Total length of poll packet */
#define LENROUT         256             /* Maximum length of rout packet */
#define MAXDELAY        3000            /* Maximum delay (5 minutes) */
#define POLLINTERVAL    (5*60*1000L)    /* Time between polls (5 minutes) */

#define FLEX_INIT       '0'     /* Link initialization */
#define FLEX_RPRT       '1'     /* Poll answer */
#define FLEX_POLL       '2'     /* Poll */
#define FLEX_ROUT       '3'     /* Routing information */
#define FLEX_QURY       '6'     /* Path query */
#define FLEX_RSLT       '7'     /* Query result */

struct peer {
	struct peer *next;      /* Linked-list pointer */
	char call[FLEXLEN];     /* Flexnet call */
	int remdelay;           /* RTT measured remotely (100ms steps) */
	int locdelay;           /* RTT measured locally (100ms steps) */
	double delay;           /* Smoothed delay (100ms units) */
	int32 lastpolltime;     /* Time of last poll (ms) */
	struct ax25_cb *axp;    /* AX.25 control block pointer */
	int id;                 /* Last AX.25 control block id */
	int havetoken;          /* True if allowed to send routing infos */
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
	call[ALEN+1] = call[ALEN];
	return 0;
}

/*---------------------------------------------------------------------------*/

#if 0

static int flexsetcall_no_mesg(char *call, const char *ascii)
{
	if (setcall(call, (char *) ascii))
		return (-1);
	call[ALEN+1] = call[ALEN];
	return 0;
}

#endif

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
	struct ax_route *rpd;
	struct ax_route *rpp;
	struct neighbor *pn;

	if (!(pn = find_best_neighbor(pd)))
		return;
	if (!(rpp = ax_routeptr(pn->peer->call, 0)))
		return;
	dest_is_peer = addreq(pd->call, pn->peer->call);
	for (addrcp(call, pd->call); ; call[ALEN] += 2) {
		if (!(rpd = ax_routeptr(call, 1)))
			return;
		if (!rpd->perm) {
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
		if ((call[ALEN] & SSID) >= (pd->call[ALEN+1] & SSID))
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

	for (ppd = &Dests; (pd = *ppd); ) {
		for (ppn = &pd->neighbors; (pn = *ppn); ) {
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

	/* Find or create destination */

	if (!(pd = find_dest(destcall))) {
		if (!delay)
			return;
		if (!(pd = (struct dest *) calloc(1, sizeof(struct dest ))))
			return;
		pd->next = Dests;
		Dests = pd;
		flexaddrcp(pd->call, destcall);
	}

	/* Find or create neighbor */

	if (!(pn = find_neighbor(pd, pp))) {
		if (!delay)
			return;
		if (!(pn = (struct neighbor *) calloc(1, sizeof(struct neighbor ))))
			return;
		pn->next = pd->neighbors;
		pd->neighbors = pn;
		pn->peer = pp;
	}

	/* Update delay */

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

static struct ax25_cb *setaxp(struct peer *pp)
{

	struct ax25 hdr;
	struct dest *pd;
	struct neighbor *pn;

	if (!(pp->axp = find_ax25(pp->call))) {
		memset((char *) & hdr, 0, sizeof(struct ax25 ));
		addrcp(hdr.dest, pp->call);
		if (!(pp->axp = open_ax25(&hdr, AX_ACTIVE, 0, 0, 0, 0)))
			return 0;
	}
	if (pp->id != pp->axp->id) {
		pp->id = pp->axp->id;
		pp->lastpolltime = 0;
		for (pd = Dests; pd; pd = pd->next)
			if ((pn = find_neighbor(pd, pp)))
				pn->delay = pn->lastdelay = 0;
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
			if (!(pnpeer = (struct neighbor *) calloc(1, sizeof(struct neighbor ))))
				break;
			pnpeer->next = pd->neighbors;
			pd->neighbors = pnpeer;
			pnpeer->peer = pp;
		}
		delay = MAXDELAY + 1;
		pnbest = find_best_neighbor(pd);
		if (pnbest && pnbest != pnpeer)
			delay = pnbest->delay;
		if (!(lastdelay = pnpeer->lastdelay)) lastdelay = MAXDELAY + 1;
		if (delay > lastdelay || iround(1.25 * delay) < lastdelay) {
			if (!pp->havetoken) {
				if ((bp = alloc_mbuf(3))) {
					cp = bp->data;
					*cp++ = FLEX_ROUT;
					*cp++ = '+';
					*cp++ = '\r';
					bp->cnt = 3;
					send_ax25(pp->axp, bp, PID_FLEXNET);
				}
				return;
			}
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
			*cp++ = '0' + ((pd->call[ALEN+1] & SSID) >> 1);
			if (delay > MAXDELAY) delay = 0;
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

static char *sprintflexcall(char *buf, const char *addr)
{

	char *cp;
	int chr;
	int i;
	int max_ssid;
	int min_ssid;

	cp = buf;
	for (i = 0; i < ALEN; i++) {
		chr = (*addr++ >> 1) & 0x7f;
		if (chr == ' ')
			break;
		*cp++ = chr;
	}
	min_ssid = (*addr++ & SSID) >> 1;
	max_ssid = (*addr & SSID) >> 1;
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

static void polltimer_expired(void *unused)
{
	struct peer *pp;

	start_timer(&Polltimer);
	for (pp = Peers; pp; pp = pp->next) {
		if (pp->lastpolltime)
			pp->id--;
		if (setaxp(pp) && !pp->lastpolltime)
			send_poll(pp);
	}
	process_changes();
}

/*---------------------------------------------------------------------------*/

static struct peer *create_peer(const char *call)
{
	struct peer *pp;

	if ((pp = (struct peer *) calloc(1, sizeof(struct peer )))) {
		if (!Peers) {
			Polltimer.func = polltimer_expired;
			Polltimer.arg = 0;
			set_timer(&Polltimer, POLLINTERVAL);
			start_timer(&Polltimer);
		}
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
	int lowest;
	int newlowest;
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
			for (lowest = 1; lowest != (newlowest = 0x7fffffff); lowest = newlowest)
				for (pn = pd->neighbors; pn; pn = pn->next)
					if (pn->delay == lowest)
						printf("  %s (%d)", sprintflexcall(buf, pn->peer->call), pn->delay);
					else if (pn->delay > lowest && pn->delay < newlowest)
						newlowest = pn->delay;
			putchar('\n');
		}
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinkadd(int argc, char *argv[], void *p)
{
	char call[FLEXLEN];

	if (flexsetcall(call, argv[1]))
		return 1;
	if (ismyax25addr(call)) {
		printf("Cannot add link to myself\n");
		return 1;
	}
	if (find_peer(call)) {
		printf("Duplicate link \"%s\"\n", argv[1]);
		return 1;
	}
	if (!create_peer(call)) {
		printf(Nospace);
		return 1;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinkdel(int argc, char *argv[], void *p)
{

	char call[FLEXLEN];
	struct dest *pd;
	struct neighbor **ppn;
	struct neighbor *pn;
	struct peer **ppp;
	struct peer *pp;

	if (flexsetcall(call, argv[1]))
		return 1;
	for (ppp = &Peers; (pp = *ppp); ) {
		if (addreq(pp->call, call)) {
			for (pd = Dests; pd; pd = pd->next) {
				for (ppn = &pd->neighbors; (pn = *ppn); ) {
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
			process_changes();
			return 0;
		} else {
			ppp = &pp->next;
		}
	}
	printf("Link \"%s\" not found\n", argv[1]);
	return 1;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinklist(int argc, char *argv[], void *p)
{

	char buf[FLEXBUF];
	int state;
	struct ax25_cb *axp;
	struct peer *pp;

	printf("Call         Remote  Local Smooth State\n");
	for (pp = Peers; pp; pp = pp->next) {
		state = (axp = find_ax25(pp->call)) ? axp->state : LAPB_DISCONNECTED;
		printf("%-12s %6d %6d %6d %s\n", sprintflexcall(buf, pp->call), pp->remdelay, pp->locdelay, iround(pp->delay), Ax25states[state]);
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

#if 0

static int doflexnetquery(int argc, char *argv[], void *p)
{

	char asciicall[AXBUF];
	char call[FLEXLEN];
	char destcall[FLEXBUF];
	char myasciicall[AXBUF];
	char viacall[AXBUF];
	struct dest *p1;
	struct mbuf *bp;
	struct neighbor *p2;
	struct peer *p3;

	if (flexsetcall(call, argv[1]))
		return 1;
	pax25(asciicall, call);
	pax25(myasciicall, Mycall);
	if (ismyax25addr(call)) {
		printf("Cannot query myself\n");
		return 1;
	}
	if (!(p1 = find_dest(call))) {
		printf("Destination \"%s\" not found\n", argv[1]);
		return 1;
	}
	sprintflexcall(destcall, p1->call);
	if (!(p2 = find_best_neighbor(p1)))
		goto error;
	printf("%s, %d\n", destcall, p2->delay);
	if (addreq(p1->call, p2->peer->call)) {
		printf("*** flexnet routing: %s %s\n", myasciicall, asciicall);
		return 1;
	}
	p3 = p2->peer;
	if (!setaxp(p3))
		goto error;
	pax25(viacall, p3->call);
	bp = alloc_mbuf(200);
	if (!bp)
		goto error;
	sprintf(bp->data, "%c!00000%s %s %s \r", FLEX_QURY, myasciicall, viacall, asciicall);
	bp->cnt = strlen(bp->data);
	send_ax25(p3->axp, bp, PID_FLEXNET);
	return 0;

error:
	printf("Usage: flexnet query <call>\n");  /*****???????????*******/
	return 1;
}

#endif

/*---------------------------------------------------------------------------*/

int doflexnet(int argc, char *argv[], void *p)
{

	static struct cmds Flexnetcmds[] = {
		{ "dest",      doflexnetdest,      0, 0, 0 },
		{ "link",      doflexnetlink,      0, 0, 0 },
#if 0
		{ "query",     doflexnetquery,     0, 2, "flexnet query <call>" },
#endif
		{ 0,           0,                  0, 0, 0 }
	};

	return subcmd(Flexnetcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int pullnumber(struct mbuf **bpp, int *val)
{
	int chr;

	*val = 0;
	for (; ; ) {
		chr = PULLCHAR(bpp);
		if (chr == -1 || chr < '0' || chr > '9')
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
		pp->call[ALEN+1] = ((chr << 1) & SSID) | 0x60;
	free_p(bp);
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
		pp->locdelay = (int) ((msclock() - pp->lastpolltime + 50) / 100);
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

static void recv_poll(struct peer *pp, struct mbuf *bp)
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

	for (; ; ) {
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
				pp->havetoken = 0;
			}
			break;
		} else if (chr == '-') {
			pp->havetoken = 1;
			break;
		} else {
			break;
		}
	}
	free_p(bp);
}

/*---------------------------------------------------------------------------*/

#if 0

static void recv_rslt(struct peer *pe, struct mbuf *bp)
{

	char *cp;
	char call2[FLEXLEN];
	char call[AXBUF];
	int i;
	struct dest *pd;
	struct neighbor *pn;
	struct peer *pp;

	*bp->data = FLEX_RSLT; /* restore packet type */
	for (cp = call, i = 0; i < AXBUF - 1; i++) {
		if ((*cp = bp->data[7+i]) == ' ')
			break;
		cp++;
	}
	*cp = 0;
	if (flexsetcall_no_mesg(call2, call))
		goto discard;
	if (addreq(call2, Mycall)) {
		for (cp = bp->data + 7, i = bp->cnt - 7; (i > 0) && (*cp != '\r'); cp++, i--)
			;
		*cp = 0;
		printf("*** flexnet routing: %s\n", bp->data + 7);
		goto discard;
	}
	if (!(pd = find_dest(call2)))
		goto discard;
	if (!(pn = find_best_neighbor(pd)))
		goto discard;
	pp = pn->peer;
	if (!setaxp(pp))
		goto discard;
	send_ax25(pp->axp, bp, PID_FLEXNET);
	return;

discard:
	free_p(bp);
}

#endif

/*---------------------------------------------------------------------------*/

#if 0

static void recv_qury(struct peer *pe, struct mbuf *bp)
{

	char *cp;
	char call[FLEXLEN];
	char rcall[AXBUF], dcall[AXBUF];
	int i, j;
	struct dest *p;
	struct mbuf *bp2;
	struct neighbor *p1;
	struct peer *p2;

	if (!(bp2 = alloc_mbuf(pe->axp->paclen)))
		goto discard;
	bp2->data[bp2->cnt++] = FLEX_QURY; /* new packet is too routing enq */
	if ((i = PULLCHAR(&bp)) < ' ')
		goto discard; /* this is the current number of entries */
	i -= ' ';
	bp2->data[bp2->cnt++] = '!' + i;
	/* now copy the five chars of the QSO number */
	for (j = 0; j < 5; j++)
		if ((bp2->data[bp2->cnt++] = PULLCHAR(&bp)) < ' ')
			goto discard;
	for (; ; ) {
		if (bp2->cnt >= pe->axp->paclen)
			goto discard;
		if ((bp2->data[bp2->cnt] = PULLCHAR(&bp)) < ' ')
			goto discard;
		if (bp2->data[bp2->cnt++] == ' ')
			if (i > 0)
				i--;
			else
				break;
	}
	cp = rcall;
	for (j = 0; j < AXBUF - 1; j++) {
		if ((*cp = PULLCHAR(&bp)) <= ' ')
			break;
		cp++;
	}
	*cp = 0;
	free_p(bp);
	if (flexsetcall_no_mesg(call, rcall))
		goto discard;
	if (!(p = find_dest(call)))
		goto discard;
	if (!(p1 = find_best_neighbor(p)))
		goto discard;
	if (addreq(p->call, p1->peer->call)) { /* make a type 7 packet and append the call */
		for (cp = rcall; *cp; cp++) {
			if (bp2->cnt >= pe->axp->paclen - 1)
				goto discard;
			bp2->data[bp2->cnt++] = *cp;
		}
		bp2->data[bp2->cnt++] = '\r';
		recv_rslt(0, bp2); /***********/
		return;
	}
	p2 = p1->peer;
	pax25(dcall, p2->call);
	for (cp = dcall; *cp; cp++) {
		if (bp2->cnt >= pe->axp->paclen - 1)
			goto discard;
		bp2->data[bp2->cnt++] = *cp;
	}
	bp2->data[bp2->cnt++] = ' ';
	for (cp = rcall; *cp; cp++) {
		if (bp2->cnt >= pe->axp->paclen - 1)
			goto discard;
		bp2->data[bp2->cnt++] = *cp;
	}
	bp2->data[bp2->cnt++] = '\r';
	send_ax25(pe->axp, bp2, PID_FLEXNET);
	return;
discard:
	free_p(bp2);
	free_p(bp);
}

#endif

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
		call[ALEN+1] = call[ALEN];
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

#if 0
	case FLEX_QURY:
		recv_qury(pp, bp);
		break;

	case FLEX_RSLT:
		bp = pushdown(bp, 1);
		recv_rslt(pp, bp);
		break;
#endif

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

	char buf[16];
	char call[16];
	int chr;
	int func;
	int i;

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
		fprintf(fp, " - RTT: %d", i);
		break;

	case FLEX_POLL:
		fprintf(fp, " Poll");
		break;

	case FLEX_ROUT:
		fprintf(fp, " Routing:");
		for (; ; ) {
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
		fprintf(fp, " Route query:\n  ");
		while ((chr = PULLCHAR(bpp)) != -1 && chr != '\r')
			putc(chr, fp);
		break;

	case FLEX_RSLT:
		fprintf(fp, " Query response:\n  ");
		while ((chr = PULLCHAR(bpp)) != -1 && chr != '\r')
			putc(chr, fp);
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

