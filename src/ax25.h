/* @(#) $Header: /home/deyke/tmp/cvs/tcp/src/ax25.h,v 1.3 1990-09-11 13:44:58 deyke Exp $ */

#ifndef AX25_INCLUDED
#define AX25_INCLUDED

#include "global.h"
#include "iface.h"

/* AX.25 datagram (address) sub-layer definitions */

/* Upper sub-layer (LAPB) definitions */

/* Control field templates */
#define I       0x00    /* Information frames */
#define S       0x01    /* Supervisory frames */
#define RR      0x01    /* Receiver ready */
#define RNR     0x05    /* Receiver not ready */
#define REJ     0x09    /* Reject */
#define U       0x03    /* Unnumbered frames */
#define SABM    0x2f    /* Set Asynchronous Balanced Mode */
#define DISC    0x43    /* Disconnect */
#define DM      0x0f    /* Disconnected mode */
#define UA      0x63    /* Unnumbered acknowledge */
#define FRMR    0x87    /* Frame reject */
#define UI      0x03    /* Unnumbered information */
#define PF      0x10    /* Poll/final bit */

#define MMASK   7       /* Mask for modulo-8 sequence numbers */

/* FRMR reason bits */
#define W       1       /* Invalid control field */
#define X       2       /* Unallowed I-field */
#define Y       4       /* Too-long I-field */
#define Z       8       /* Invalid sequence number */

/* Maximum number of digipeaters */
#define MAXDIGIS        7       /* 7 digipeaters plus src/dest */
#define ALEN            6       /* Number of chars in callsign field */
#define AXALEN          7       /* Total AX.25 address length, including SSID */
#define AXBUF           10      /* Buffer size for maximum-length ascii call */

/* Internal representation of an AX.25 address */
struct ax25_addr {
	char call[ALEN];
	char ssid;
#define SSID            0x1e    /* Sub station ID */
#define REPEATED        0x80    /* Has-been-repeated bit in repeater field */
#define E               0x01    /* Address extension bit */
#define C               0x80    /* Command/response designation */
};
#define NULLAXADDR      (struct ax25_addr *)0
/* Our AX.25 address */
extern char Mycall[AXALEN];

/* AX.25 broadcast address: "QST   -0" in shifted ASCII */
extern struct ax25_addr Ax25_bdcst;

/* Internal representation of an AX.25 header */
struct ax25 {
	struct ax25_addr dest;                  /* Destination address */
	struct ax25_addr source;                /* Source address */
	struct ax25_addr digis[MAXDIGIS];       /* Digi string */
	int ndigis;                             /* Number of digipeaters */
	int cmdrsp;                             /* Command/response */
};

/* C-bit stuff */
#define LAPB_UNKNOWN            0
#define LAPB_COMMAND            1
#define LAPB_RESPONSE           2

/* AX.25 Level 3 Protocol IDs (PIDs) */
#define PID_X25         0x01    /* CCITT X.25 PLP */
#define PID_SEGMENT     0x08    /* Segmentation fragment */
#define PID_TEXNET      0xc3    /* TEXNET datagram protocol */
#define PID_LQ          0xc4    /* Link quality protocol */
#define PID_APPLETALK   0xca    /* Appletalk */
#define PID_APPLEARP    0xcb    /* Appletalk ARP */
#define PID_IP          0xcc    /* ARPA Internet Protocol */
#define PID_ARP         0xcd    /* ARPA Address Resolution Protocol */
#define PID_FLEXNET     0xce    /* FLEXNET */
#define PID_NETROM      0xcf    /* NET/ROM */
#define PID_NO_L3       0xf0    /* No level 3 protocol */

#define SEG_FIRST       0x80    /* First segment of a sequence */
#define SEG_REM         0x7f    /* Mask for # segments remaining */

#define axptr(a)         ((struct ax25_addr *) (a))
#define ismycall(call)   addreq(call, Mycall)

/* ax25.c */
int ax_send __ARGS((struct mbuf *bp, struct iface *iface, int32 gateway, int precedence, int delay, int throughput, int reliability));
int ax_output __ARGS((struct iface *iface, char *dest, char *source, int pid, struct mbuf *data));
void ax_recv __ARGS((struct iface *iface, struct mbuf *bp));
int axarp __ARGS((void));

/* ax25subr.c */
int setcall __ARGS((char *out,char *call));
int setpath __ARGS((char *out, char *in [], int cnt));
int addreq __ARGS((char *a, char *b));
void addrcp __ARGS((char *to, char *from));
char *pax25 __ARGS((char *e,char *addr));
char *psax25 __ARGS((char *e, char *addr));
char *getaxaddr __ARGS((struct ax25_addr *ap, char *cp));
char *putaxaddr __ARGS((char *cp, struct ax25_addr *ap));
struct mbuf *htonax25 __ARGS((struct ax25 *hdr, struct mbuf *data));
int atohax25 __ARGS((struct ax25 *hdr, char *hwaddr, struct ax25_addr *source));
int ntohax25 __ARGS((struct ax25 *hdr, struct mbuf **bpp));
int16 ftype __ARGS((int control));

/* idigi.c */
int idigi __ARGS((struct iface *ifp, struct mbuf *bp));
int doidigi __ARGS((int argc, char *argv [], void *p));

#endif  /* AX25_INCLUDED */

