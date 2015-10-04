/*
 * chaos.c
 * userspace chaos protocol implementation
 *
 * original; Brad Parker <brad@heeltoe.com>
 * byte order cleanups; Joseph Oswald <josephoswald@gmail.com>
 *
 * $Id: chaos.c 78 2006-07-18 18:28:34Z brad $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/types.h>
#include <sys/uio.h>

#include "chaos.h"
#include "ncp.h"
#include "server.h"
#include "log.h"
#include "FILE.h"
#include "rfc.h"

struct chroute chaos_routetab[256];
struct connection *chaos_conntab[256];
int chaos_myaddr;
char chaos_myname[32];

int chaos_nobridge;
int chaos_rfcrcv;
int chaos_clock;
int chaos_error;

void rcvpkt(struct chxcvr *xp);
struct packet *ch_alloc_pkt(int datalen);
void ch_free_pkt(struct packet *pkt);
void sendctl(struct packet *pkt);
void chmove(char *from, char *to, int n);
void reflect(struct packet *pkt);
void senddata(struct packet *pkt);
void makests(struct connection *conn, struct packet *pkt);
int concmp(struct packet *rfcpkt, char *lsnstr, int lsnlen);
void lsnmatch(struct packet *rfcpkt, struct connection *conn);
void clsconn(struct connection *conn, int state, struct packet *pkt);
void rmlisten(struct connection *conn);
void rlsconn(struct connection *conn);


struct packet *chaos_lsnlist;	/* listening connections */

struct packet *chaos_rfclist;
struct packet *chaos_rfctail;

#define INPUT(x) server_input(x)
#define OUTPUT(x)
#define NEWSTATE(x)
#define RFCINPUT

/*
 * Zero out n bytes
 */
void
clear(char *ptr, int n)
{
	if (n)
		do {
			*ptr++ = 0;
		} while (--n);
}

/*
 * Move contents of opkt to npkt
 */
void
movepkt(struct packet *opkt, struct packet *npkt)
{
	short *nptr, *optr, n;

	n = (CHHEADSIZE + PH_LEN(opkt->pk_phead) + 
	     sizeof(short) - 1) / sizeof(short);
	nptr = (short *)npkt;
	optr = (short *)opkt;
	do {
		*nptr++ = *optr++;
	} while (--n);
}

/*
 * Move n bytes
 */
void
chmove(char *from, char *to, int n)
{
	if (n)
		do *to++ = *from++; while(--n);
}


struct packet *
ch_alloc_pkt(int data_size)
{
	struct packet *pkt;
	int alloc_size = sizeof(struct packet) + data_size;

	tracef(TRACE_LOW, "ch_alloc_pkt(size=%d)", data_size);

	pkt = (struct packet *)malloc(alloc_size);
	if (pkt == 0)
		return NULL;

	if (1) memset((char *)pkt, 0, alloc_size);

	return pkt;
}

void
ch_free_pkt(struct packet *pkt)
{
	free((char *)pkt);
}

/*
 * Free a list of packets
 */
void
freelist(struct packet *pkt)
{
	struct packet *opkt;

	while ((opkt = pkt) != NOPKT) {
		pkt = pkt->pk_next;
		ch_free_pkt(opkt);
	}
}

/*
 * Fill a packet with a string, returning packet because it may reallocate
 * If the pkt argument is NOPKT then allocate a packet here.
 * The string is null terminated and may be shorter than "len".
 */
struct packet *
pktstr(struct packet *pkt, char *str, int len)
{
	struct packet *npkt;
	register char *odata;

	if (pkt == NOPKT /*|| ch_size((char *)pkt) < CHHEADSIZE + len*/ ) {
		if ((npkt = ch_alloc_pkt(len)) == NOPKT)
			return(NOPKT);
		if (pkt != NOPKT) {
		  SET_PH_LEN(pkt->pk_phead,0);
			movepkt(pkt, npkt);
			ch_free_pkt(pkt);
		}
		pkt = npkt;
	}
	odata = pkt->pk_cdata;
	SET_PH_LEN(pkt->pk_phead,len);
	if (len) do *odata++ = *str; while (*str++ && --len);
	return(pkt);
}

static int uniq;

/*
 * Allocate a connection and return it, also allocating a slot in Chconntab
 */
struct connection *
allconn(void)
{
	struct connection *conn;
	struct connection **cptr;
//	static int uniq;

	if ((conn = (struct connection *)malloc(sizeof(struct connection))) == NOCONN) {
		debugf(DBG_WARN, "allconn: alloc failed (connection)");
		chaos_error = CHNOPKT;
		return(NOCONN);
	}

	for(cptr = &chaos_conntab[0]; cptr != &chaos_conntab[CHNCONNS]; cptr++) {
		if(*cptr != NOCONN) continue;
		*cptr = conn;
		clear((char *)conn, sizeof(struct connection));
		SET_CH_INDEX(conn->cn_Lidx,cptr - &chaos_conntab[0]);
		if (++uniq == 0)
			uniq = 1;
		conn->cn_luniq = uniq;
		debugf(DBG_LOW, "allconn: alloc #%x", 
		       CH_INDEX_SHORT(conn->cn_Lidx));
		return(conn);
	}

	free((char *)conn);
	chaos_error = CHNOCONN;
	debugf(DBG_WARN, "allconn: alloc failed (table)");

	return(NOCONN);
}

/*
 * Make a connection closed with given state, at interrupt time.
 * Queue the given packet on the input queue for the user.
 */
void
clsconn(struct connection *conn, int state, struct packet *pkt)
{
	freelist(conn->cn_thead);
	conn->cn_thead = conn->cn_ttail = NOPKT;
	conn->cn_state = state;
	debugf(DBG_LOW, "clsconn: Conn #%x: CLOSED: state: %d",
	       CH_INDEX_SHORT(conn->cn_Lidx), state);
	if (pkt != NOPKT) {
		pkt->pk_next = NOPKT;
		if (conn->cn_rhead != NOPKT)
			conn->cn_rtail->pk_next = pkt;
		else
			conn->cn_rhead = pkt;
		conn->cn_rtail = pkt;
	}
	NEWSTATE(conn);
}
	
/*
 * Release a connection - freeing all associated storage.
 * This removes all trace of the connection.
 * Always called from top level at low priority.
 */
void
rlsconn(struct connection *conn)
{
	chaos_conntab[conn->cn_ltidx] = NOCONN;
	freelist(conn->cn_routorder);
	freelist(conn->cn_rhead);
	freelist(conn->cn_thead);

	debugf(DBG_LOW, "rlsconn: release #%x", CH_INDEX_SHORT(conn->cn_Lidx));
	free((char *)conn);
}

void
prpkt(struct packet *pkt, char *str)
{
	debugf(DBG_LOW, "op=%s(%o) len=%d fc=%d; dhost=%o didx=%x; shost=%o sidx=%x\npkn=%d ackn=%d",
		str, pkt->pk_op, PH_LEN(pkt->pk_phead), 
	        PH_FC(pkt->pk_phead), pkt->pk_dhost,
		CH_INDEX_SHORT(pkt->pk_phead.ph_didx), pkt->pk_shost, 
	        CH_INDEX_SHORT(pkt->pk_phead.ph_sidx),
	       (unsigned)LE_TO_SHORT(pkt->LE_pk_pkn), 
	       (unsigned)LE_TO_SHORT(pkt->LE_pk_ackn));
}

/*
 * Make a STS packet for a connection, reflecting its current state
 */
void
makests(struct connection *conn, struct packet *pkt)
{
	pkt->pk_op = STSOP;
	SET_PH_LEN(pkt->pk_phead, sizeof(struct sts_data));
	setack(conn, pkt);
	pkt->LE_pk_receipt = LE_TO_SHORT(conn->cn_rlast);
	pkt->LE_pk_rwsize = LE_TO_SHORT(conn->cn_rwsize);
}


/*
 * Process receipts and acknowledgements using recnum as the receipt.
 */
void
receipt(struct connection *conn, unsigned short acknum, unsigned short recnum)
{
	struct packet *pkt, *pktl;

	/*
	 * Process a receipt, freeing packets that we now know have been
	 * received.
	 */
	if (cmp_gt(recnum, conn->cn_trecvd)) {
		for (pktl = conn->cn_thead;
		     pktl != NOPKT && cmp_le(LE_TO_SHORT(pktl->LE_pk_pkn), 
					     recnum);
		     pktl = pkt)
		{
			pkt = pktl->pk_next;
			ch_free_pkt(pktl);
		}
		if ((conn->cn_thead = pktl) == NOPKT)
			conn->cn_ttail = NOPKT;
		conn->cn_trecvd = recnum;
	}

	/*
	 * If the acknowledgement is new, update our idea of the
	 * latest acknowledged packet, and wakeup output that might be blocked
	 * on a full transmit window.
	 */
	if (cmp_gt(acknum, conn->cn_tacked)) {
          if (cmp_gt(acknum, conn->cn_tlast)) {
            debugf(DBG_WARN,
                   "receipt: Invalid acknowledgment(%d,%d)",
                   acknum, conn->cn_tlast);
          }
          else {
            conn->cn_tacked = acknum;
            OUTPUT(conn);
          }
        }
}

/*
 * Indicate that actual transmission of the current packet has been completed.
 * Called by the device dependent interrupt routine when transmission
 * of a packet has finished.
 */
void
xmitdone(struct packet *pkt)
{
	struct connection *conn;
	struct packet *npkt;

	if (PH_FC(pkt->pk_phead) == 0 && CONTPKT(pkt) &&
            // always true:
            // warning: comparison of constant 256 with expression of type 'unsigned char' is always true [-Wtautological-constant-out-of-range-compare]
	    //pkt->pk_stindex < CHNCONNS &&
	    (conn = chaos_conntab[pkt->pk_stindex]) != NOCONN &&
	    CH_INDEX_SHORT(pkt->pk_sidx) == 
	    CH_INDEX_SHORT(conn->cn_Lidx) &&
	    (conn->cn_state == CSOPEN || conn->cn_state == CSRFCSENT) &&
	    cmp_gt(LE_TO_SHORT(pkt->LE_pk_pkn), conn->cn_trecvd))
	{
		pkt->pk_time = chaos_clock;

		if ((npkt = conn->cn_thead) == NOPKT || 
		    cmp_lt(LE_TO_SHORT(pkt->LE_pk_pkn), 
			   LE_TO_SHORT(npkt->LE_pk_pkn)))
		{
			pkt->pk_next = npkt;
			conn->cn_thead = pkt;
		} else {
			for( ; npkt->pk_next != NOPKT; npkt = npkt->pk_next)
				if(cmp_lt(LE_TO_SHORT(pkt->LE_pk_pkn), 
					  LE_TO_SHORT(npkt->pk_next->LE_pk_pkn)))
					break;
			pkt->pk_next = npkt->pk_next;
			npkt->pk_next = pkt;
		}
		if(pkt->pk_next == NOPKT)
			conn->cn_ttail = pkt;
	} else
		ch_free_pkt(pkt);
}

/*
 * Return the next packet on which to begin transmission (if none,  NOPKT).
 */
struct packet *
xmitnext(struct chxcvr *xcvr)
{
	struct packet *pkt;

	if ((pkt = xcvr->xc_tpkt = xcvr->xc_list) != NOPKT) {
		if ((xcvr->xc_list = pkt->pk_next) == NOPKT)
			xcvr->xc_tail = NOPKT;
		xcvr->xc_ttime = chaos_clock;
	}

	return pkt;
}

void
chretran(struct connection *conn, int age)
{
	struct packet *pkt, **opkt;
	struct packet *lastpkt;
	struct packet *firstpkt = NOPKT;

	for (opkt = &conn->cn_thead; (pkt = *opkt) != 0;)
		if (cmp_gt(chaos_clock, pkt->pk_time + age)) {
			if (firstpkt == NOPKT) 
				firstpkt = pkt;
			else
				lastpkt->pk_next = pkt;

			lastpkt = pkt;
			*opkt = pkt->pk_next;
			pkt->pk_next = NOPKT;
			setack(conn, pkt);
		} else
			opkt = &pkt->pk_next;

	if (firstpkt != NOPKT) {
		debugf(DBG_LOW, "chretran: Conn #%x: Rexmit (op:%d, pkn:%d)",
		       CH_INDEX_SHORT(conn->cn_Lidx), firstpkt->pk_op,
		       LE_TO_SHORT(firstpkt->LE_pk_pkn));
		senddata(firstpkt);
	}
}

/*
 * Send the (list of) packet(s) to myself - NOTE THIS CAN BE RECURSIVE!
 */
void
sendtome(register struct packet *pkt)
{
	struct packet *rpkt, *npkt;
	static struct chxcvr fakexcvr;

	tracef(TRACE_LOW, "sendtome()");

	/*
	 * Static structure is used to economize on stack space.
	 * We are careful to use it very locally so that recursion still
	 * works. Cleaner solutions don't recurse very well.
	 * Basically for each packet in the list, prepare it for
	 * transmission by executing xmitnext,
	 * complete the transmission by calling xmitdone,
	 * then receive the packet on the other side, possibly
	 * causing this routine to recurse (after we're done with the
	 * static structure until next time around the loop)
	 * When this routine is called with a list of data packets, things
	 * can get pretty weird.
	 */
	while (pkt != NOPKT) {
		/*
		 * Make the transmit list consist of one packet to send.
		 * Save the rest of the list in npkt.
		 */
		npkt = pkt->pk_next;
		pkt->pk_next = NOPKT;
		fakexcvr.xc_tpkt = NOPKT;
		fakexcvr.xc_list = fakexcvr.xc_tail = pkt;
		/*
		 * The xmitnext just dequeues pkt and sets ackn.
		 * It will free the packet if its not worth sending.
		 */
		(void)xmitnext(&fakexcvr);
		if (fakexcvr.xc_tpkt) {
			/*
			 * So it really should be sent.
			 * First make a copy for the receiving side in rpkt.
			 */
			if ((rpkt = ch_alloc_pkt(PH_LEN(pkt->pk_phead))) 
			    != NOPKT)
				movepkt(pkt, rpkt);
			/*
			 * This xmitdone just completes transmission.
			 * Now pkt is out of our hands.
			 */
			xmitdone(pkt);
			/*
			 * So transmission is complete. Now receive it.
			 */
			if (rpkt != NOPKT) {
				fakexcvr.xc_rpkt = rpkt;
				rcvpkt(&fakexcvr);
			}
		}
		pkt = npkt;
	}
}

/*
 * Sendctl - send a control packet that will not be acknowledged and
 *	will not be retransmitted (this actual packet).  Put the
 *	given packet at the head of the transmit queue so it is transmitted
 *	"quickly", i.e. before data packets queued at the tail of the queue.
 *	If anything is wrong (no path, bad subnet) we just throw it
 *	away. Remember, pk_next is assumed to be == NOPKT.
 */
void
sendctl(struct packet *pkt)
{
	struct chroute *r;

	if (1) {
		debugf(DBG_LOW, "chaos: sendctl: Sending: %d ", pkt->pk_op);
		prpkt(pkt, "ctl");
		debugf(DBG_LOW, "\n");
	}
	if (CH_ADDR_SHORT(pkt->pk_daddr) == chaos_myaddr) {
		debugf(DBG_LOW, "to me");
		sendtome(pkt);
	} else if (pkt->pk_dsubnet >= CHNSUBNET ||
	    (r = &chaos_routetab[pkt->pk_dsubnet])->rt_type == CHNOPATH ||
	     r->rt_cost >= CHHCOST) {
		debugf(DBG_LOW, "chaos: Dropping, no route");
		ch_free_pkt(pkt);
	} else {
		if (r->rt_type == CHFIXED || r->rt_type == CHBRIDGE) {
			pkt->pk_xdest = r->rt_addr;
			r = &chaos_routetab[r->rt_subnet];
		} else
			pkt->pk_xdest = pkt->pk_daddr;
		(*r->rt_xcvr->xc_xmit)(r->rt_xcvr, pkt, 1);
	}
}

/*
 * Senddata - send a list of controlled packets, all assumed to be to the
 *	same destination.  Queue them on the end of the appropriate transmit
 *	queue.
 *	Similar to sendctl, but stuffs time, handles a list, and puts pkts
 *	at the end of the queue instead of at the beginning, and fakes
 *	transmission if it can't really send the packets (as opposed to
 *	throwing the packets away)
 */
void
senddata(struct packet *pkt)
{
	struct chroute *r;

	if (1) {
		debugf(DBG_LOW, "Sending: %d ", pkt->pk_op);
		prpkt(pkt, "data");
		debugf(DBG_LOW, "\n");
	}

	if (CH_ADDR_SHORT(pkt->pk_daddr) == chaos_myaddr) {
		debugf(DBG_LOW, "to me");
		sendtome(pkt);
        }
	else if (pkt->pk_dsubnet >= CHNSUBNET ||
	    (r = &chaos_routetab[pkt->pk_dsubnet])->rt_type == CHNOPATH ||
	     r->rt_cost >= CHHCOST) {
		struct packet *npkt;
		debugf(DBG_LOW, "no path to 0%x", CH_ADDR_SHORT(pkt->pk_daddr));
		do {
			npkt = pkt->pk_next;
			pkt->pk_next = NOPKT;
			xmitdone(pkt);
		} while ((pkt = npkt) != NOPKT);
	} else {
		struct chxcvr *xcvr;
		unsigned short dest;

		if (r->rt_type == CHFIXED || r->rt_type == CHBRIDGE) {
			dest = CH_ADDR_SHORT(r->rt_addr);
			r = &chaos_routetab[r->rt_subnet];
		} else
			dest = CH_ADDR_SHORT(pkt->pk_daddr);
		
		xcvr = r->rt_xcvr;
		for (;;) {
			struct packet *next;

			pkt->pk_time = chaos_clock;
			SET_CH_ADDR(pkt->pk_xdest,dest);
			next = pkt->pk_next;
			(*xcvr->xc_xmit)(xcvr, pkt, 0);
			if ((pkt = next) == NOPKT)
				break;
		}
	}
}

/*
 * Send a control packet back to its source
 */
void
reflect(struct packet *pkt)
{
	chindex temp_idx;
        chaddr temp_addr;

	temp_idx = pkt->pk_sidx;
	pkt->pk_sidx = pkt->pk_didx;
	pkt->pk_didx = temp_idx;
	temp_addr = pkt->pk_saddr;
	pkt->pk_saddr = pkt->pk_daddr;
	pkt->pk_daddr = temp_addr;

#if 1
	struct pkt_header *ph = (struct pkt_header *)&pkt->pk_phead;

	debugf(DBG_LOW,
	       "reflect: to (%o %o:%d,%d) from (%o %o:%d,%d) pkn %o ackn %o\n",
	       ph->ph_daddr.subnet, ph->ph_daddr.host,
	       ph->ph_didx.tidx, ph->ph_didx.uniq,
	       ph->ph_saddr.subnet, ph->ph_saddr.host,
	       ph->ph_sidx.tidx, ph->ph_sidx.uniq,
	       LE_TO_SHORT(ph->LE_ph_pkn), 
	       LE_TO_SHORT(ph->LE_ph_ackn));
#endif

	sendctl(pkt);
}

#define send_los(x,y) sendlos(x,y,sizeof(y) - 1)

/*
 * Send a LOS in response to the given packet.
 * Don't bother if the packet is itself a LOS or a CLS since the other
 * end doesn't care anyway and would only return it again.
 * Append the host name to the error message.
 */
void
sendlos(struct packet *pkt, char *str, int len)
{
	debugf(DBG_INFO, "sendlos() %s", str);

	if (pkt->pk_op == LOSOP || pkt->pk_op == CLSOP)
		ch_free_pkt(pkt);
	else {
		char *cp;
		int length;
		
		for (cp = chaos_myname, length = 0; *cp && length < CHSTATNAME;) {
			length++;
			cp++;
		}
                pkt = pktstr(pkt, str, len + length + sizeof("(from )") - 1);
		if (pkt != 0) {
			chmove("(from ", &pkt->pk_cdata[len], 6);
			chmove(chaos_myname, &pkt->pk_cdata[len + 6], length);
			chmove(")", &pkt->pk_cdata[len + 6 + length], 1);
			pkt->pk_op = LOSOP;
			reflect(pkt);
		}
	}
}

/*
 * An RFC has matched a listener, either by an RFC coming and finding a match
 * on the listener list, or by a listen being done and matching an RFC on the
 * unmatched RFC list. So we change the state of the connection to CSRFCRCVD
 */
void
lsnmatch(struct packet *rfcpkt, struct connection *conn)
{
	debugf(DBG_INFO, "lsnmatch: Conn #%x: LISTEN matched", 
	       CH_INDEX_SHORT(conn->cn_Lidx));

	/*
	 * Initialize the conection
	 */
	conn->cn_active = chaos_clock;
	conn->cn_state = CSRFCRCVD;
	if (conn->cn_rwsize == 0)
		conn->cn_rwsize = CHDRWSIZE;
	conn->cn_faddr = rfcpkt->pk_saddr;
	conn->cn_Fidx = rfcpkt->pk_sidx;
	if (CH_ADDR_SHORT(rfcpkt->pk_daddr) == 0) {
	  conn->cn_laddr = rfcpkt->pk_daddr;
	} else {
	  SET_CH_ADDR(conn->cn_laddr,chaos_myaddr);
	}

	/*
	 * Queue up the RFC for the user to read if he wants it.
	 */
	rfcpkt->pk_next = NOPKT;
	conn->cn_rhead = conn->cn_rtail = rfcpkt;
	/*
	 * Indicate to the other end that we have received and "read"
	 * the RFC so that the open will truly acknowledge it.
	 */
	conn->cn_rlast = conn->cn_rread = LE_TO_SHORT(rfcpkt->LE_pk_pkn);
}

/*
 * Remove a listener from the listener list, due to the listener bailing out.
 * Called from top level at high priority
 */
void
rmlisten(struct connection *conn)
{
	struct packet *opkt, *pkt;

	opkt = NOPKT;
	for (pkt = chaos_lsnlist; pkt != NOPKT; opkt = pkt, pkt = pkt->pk_next)
		if (pkt->pk_stindex == conn->cn_ltidx) {
			if(opkt == NOPKT)
				chaos_lsnlist = pkt->pk_next;
			else
				opkt->pk_next = pkt->pk_next;
			ch_free_pkt(pkt);
			break;
		}
}

/*
 * Compare the RFC contact name with the listener name.
 */
int
concmp(struct packet *rfcpkt, char *lsnstr, int lsnlen)
{
	char *rfcstr = rfcpkt->pk_cdata;
	int rfclen;

	debugf(DBG_LOW, "Rcvrfc: Comparing %s and %s", rfcstr, lsnstr);
	debugf(DBG_LOW, "rfcpkt->pk_len = %d lsnlen = %d", 
	       PH_LEN(rfcpkt->pk_phead),
	       lsnlen);

	for (rfclen = PH_LEN(rfcpkt->pk_phead); rfclen; rfclen--, lsnlen--) {
	  if (lsnlen <= 0) {
	    debugf(DBG_LOW, "reached lsnlen = %d *rfcstr is '%c' %x", lsnlen,
		   *rfcstr, *rfcstr);
	    debugf(DBG_LOW, "returning 1");
	    return 1;
	    /* return ((*rfcstr == ' ') ? 1 : 0); */
	  }
	  else if (*rfcstr++ != *lsnstr++) {
	    debugf(DBG_LOW, "failed to match character %x and %x",
		   *(rfcstr-1), *(lsnstr-1));
	    return(0);
	  }
	}

	return (lsnlen == 0);
}

/*
 * Process a routing packet.
 */
void
rcvrut(struct packet *pkt)
{
	int i;
	struct rut_data *rd;
	struct chroute *r;

	if (1) {
		prpkt(pkt,"RUT");
		debugf(DBG_LOW, "\n");
	}

	rd = pkt->pk_rutdata;
	if (pkt->pk_ssubnet >= CHNSUBNET)
		debugf(DBG_LOW, "CHAOS: bad subnet %d in RUT pkt",
		       pkt->pk_ssubnet);
	else if (chaos_routetab[pkt->pk_ssubnet].rt_type != CHDIRECT)
		debugf(DBG_LOW, "CHAOS: RUT pkt from unconnected subnet %d",
			pkt->pk_ssubnet);
	else for (i = PH_LEN(pkt->pk_phead); i; i -= sizeof(struct rut_data), rd++) {
		if (LE_TO_SHORT(rd->LE_pk_subnet) >= CHNSUBNET) {
			debugf(DBG_LOW, "CHAOS: bad subnet %d in RUT pkt",
			       LE_TO_SHORT(rd->LE_pk_subnet));
			continue;
		}
		r = &chaos_routetab[LE_TO_SHORT(rd->LE_pk_subnet)];
		switch (r->rt_type) {
		case CHNOPATH:
		case CHBRIDGE:
		case CHDIRECT:
			if (LE_TO_SHORT(rd->LE_pk_cost) < r->rt_cost) {
				r->rt_cost = LE_TO_SHORT(rd->LE_pk_cost);
				r->rt_type = CHBRIDGE;
				r->rt_addr = pkt->pk_saddr;
			}
			break;
		case CHFIXED:
			break;
		default:
			debugf(DBG_WARN,
			       "CHAOS: Illegal chaos routing table entry %d",
			       r->rt_type);
		}
	}
	ch_free_pkt(pkt);
}

/*
 * Process a received RFC/BRD
 */
void
rcvrfc(struct packet *pkt)
{
	struct connection *conn, **conptr;
	struct packet **opkt, *pktl;

	if (1) {
		prpkt(pkt,"RFC/BRD");
		debugf(DBG_LOW, "contact = %s", pkt->pk_cdata);
		extern int flag_debug_level;
		if (flag_debug_level > 2) {
                  dumpbuffer((u_char *)pkt, 64);
		}
	}

	/*
	 * Check if this is a duplicate RFC, and if so throw it away,
	 * and retransmit the OPEN.
	 */
	for (conptr = &chaos_conntab[0]; conptr < &chaos_conntab[CHNCONNS];)
		if ((conn = *conptr++) != NOCONN &&
		    CH_INDEX_SHORT(conn->cn_Fidx) == 
		    CH_INDEX_SHORT(pkt->pk_sidx) &&
		    CH_ADDR_SHORT(conn->cn_faddr) == 
		    CH_ADDR_SHORT(pkt->pk_saddr))
		{
			if (conn->cn_state == CSOPEN) {
				debugf(DBG_LOW,
				       "rcvrfc: Retransmitting open chan #%x",
				       CH_INDEX_SHORT(conn->cn_Lidx));
				if (conn->cn_thead != NOPKT)
					chretran(conn, CHSHORTTIME);
			} else {
				debugf(DBG_LOW, "rcvrfc: Duplicate RFC: %x",
				       CH_INDEX_SHORT(conn->cn_Lidx));
			}
			ch_free_pkt(pkt);
			return;
		}

	/*
	 * Scan the listen list for a listener and if one is found
	 * open the connection and remove the listen packet from the
	 * listen list.
	 */
	for (opkt = &chaos_lsnlist; (pktl = *opkt) != NOPKT; 
	     opkt = &pktl->pk_next)
	  if(concmp(pkt, pktl->pk_cdata, PH_LEN(pktl->pk_phead))) {
	    conn = chaos_conntab[pktl->pk_stindex];
	    *opkt = pktl->pk_next;
			ch_free_pkt(pktl);
			lsnmatch(pkt, conn);
			NEWSTATE(conn);
			return;
		}
	if (concmp(pkt, "STATUS", 6)) {
		statusrfc(pkt);
		return;
	}
	if (concmp(pkt, "TIME", 4)) {
		timerfc(pkt);
		return;
	}
	if (concmp(pkt, "UPTIME", 6)) {
		uptimerfc(pkt);
		return;
	}
	if (concmp(pkt, "DUMP-ROUTING-TABLE", 18)) {
		dumprtrfc(pkt);
		return;
	}
#if 1
	if (concmp(pkt, "FILE", 4)) {
		/* this is a hack to fake what chserver.c would do */
		struct packet *p = ch_alloc_pkt(10);
		strcpy(p->pk_cdata, "FILE");
		conn = ch_listen(p, 0);
		lsnmatch(pkt, conn);
		ch_accept(conn);

		start_file(conn, pkt->pk_cdata, PH_LEN(pkt->pk_phead));
		return;
	}
#endif
	if (chaos_rfcrcv == 0) {
		if (pkt->pk_op != BRDOP)
			send_los(pkt, "All servers disabled");
		return;
	}

	/*
	 * There was no listener, so queue the RFC on the unmatched RFC list
	 * again checking for duplicates.
	 */
	pkt->LE_pk_ackn = 0;	/* this is for ch_rskip */
	pkt->pk_next = NOPKT;
	if ((pktl = chaos_rfclist) == NOPKT) 
		chaos_rfclist = chaos_rfctail = pkt;
	else {
		do {
			if (CH_INDEX_SHORT(pktl->pk_sidx) == 
			    CH_INDEX_SHORT(pkt->pk_sidx) &&
			    CH_ADDR_SHORT(pktl->pk_saddr) == 
			    CH_ADDR_SHORT(pkt->pk_saddr)) {
				debugf(DBG_LOW,
				       "rcvrfc: Discarding duplicate Rfc on Chrfclist");
				ch_free_pkt(pkt);
				return;
			}
		} while ((pktl = pktl->pk_next) != NOPKT);
		chaos_rfctail->pk_next = pkt;
		chaos_rfctail = pkt;
	}

	debugf(DBG_INFO, "rcvrfc: Queued Rfc on chaos_rfclist: '%c%c'",
               pkt->pk_cdata[0], pkt->pk_cdata[1]);

	RFCINPUT;
}

/*
 * Process a received BRD
 */
void
rcvbrd(struct packet *pkt)
{
	int bitlen = LE_TO_SHORT(pkt->LE_pk_ackn);
	
	SET_CH_ADDR(pkt->pk_daddr,chaos_myaddr);
	chmove(&pkt->pk_cdata[bitlen], pkt->pk_cdata, bitlen);
	SET_PH_LEN(pkt->pk_phead,PH_LEN(pkt->pk_phead)-bitlen);
	pkt->LE_pk_ackn = 0;
	rcvrfc(pkt);
}

/*
 * Process a received data packet - or an EOF packet which is mostly treated
 * the same.
 */
void
rcvdata(struct connection *conn, struct packet *pkt)
{
	register struct packet *npkt;

	if (1) {
		prpkt(pkt,"DATA");
		debugf(DBG_LOW, "\n");
	}

	if (cmp_gt(LE_TO_SHORT(pkt->LE_pk_pkn), 
		   conn->cn_rread + conn->cn_rwsize)) {
		debugf(DBG_WARN, "rcvdata: Discarding data out of window");
		debugf(DBG_WARN, "pkt->pkn is %x conn->cn_rread %x",
		       LE_TO_SHORT(pkt->LE_pk_pkn), conn->cn_rread);
		debugf(DBG_WARN, "conn->cn_rwsize is %x sum %x",
		       conn->cn_rread, conn->cn_rread + conn->cn_rwsize);
		ch_free_pkt(pkt);
		return;
	}

	receipt(conn, LE_TO_SHORT(pkt->LE_pk_ackn), 
		LE_TO_SHORT(pkt->LE_pk_ackn));

	if (cmp_le(LE_TO_SHORT(pkt->LE_pk_pkn), conn->cn_rlast)) {
		debugf(DBG_WARN, "rcvdata: Duplicate data packet");
		makests(conn, pkt);
		reflect(pkt);
		return;
	}

	/*
	 * Link the out of order list onto the new packet in case
	 * it fills the gap between in-order and out-of-order lists
	 * and to make it easy to grab all now-in-order packets from the
	 * out-of-order list.
	 */
	pkt->pk_next = conn->cn_routorder;

	/*
	 * Now transfer all in-order packets to the in-order list
	 */
	for (npkt = pkt;
	     npkt != NOPKT &&
	     	LE_TO_SHORT(npkt->LE_pk_pkn)
	       == (unsigned short)(conn->cn_rlast + 1);
	     npkt = npkt->pk_next) {
		if (conn->cn_rhead == NOPKT)
			conn->cn_rhead = npkt;
		else
			conn->cn_rtail->pk_next = npkt;
		conn->cn_rtail = npkt;
		conn->cn_rlast++;
	}

	/*
	 * If we queued up any in-order pkts, check if spontaneous STS is needed
	 */
	if (pkt != npkt) {
		debugf(DBG_INFO, "rcvdata: new ordered data packet");
		conn->cn_rtail->pk_next = NOPKT;
		conn->cn_routorder = npkt;
		if (conn->cn_rhead == pkt)
			INPUT(conn);

		/*
		 * If we are buffering "many" packets, keep him up to date
		 * about the window. The second test can be flushed if it is
		 * more important to receipt than to save sending an "extra"
		 * STS with no new acknowledgement.
		 * Lets do without it for a while.
		if ((short)(conn->cn_rlast - conn->cn_racked) > conn->cn_rsts &&
		    conn->cn_racked != conn->cn_rread)
			sendsts(conn);
		 */
	/*
	 * Here we have received an out of order packet which must be
	 * inserted into the out-of-order queue, in packet number order. 
	 */
	} else {
		for (npkt = pkt; (npkt = npkt->pk_next) != NOPKT &&
				 cmp_gt(LE_TO_SHORT(pkt->LE_pk_pkn), 
					LE_TO_SHORT(npkt->LE_pk_pkn)); )

			pkt->pk_next = npkt;	/* save the last pkt here */

		if (npkt != NOPKT && LE_TO_SHORT(pkt->LE_pk_pkn) == 
		    LE_TO_SHORT(npkt->LE_pk_pkn)) {
			debugf(DBG_INFO,
			       "rcvdata: Duplicate out of order packet");
			pkt->pk_next = NOPKT;
			makests(conn, pkt);
			reflect(pkt);
		} else {
			if (npkt == conn->cn_routorder)
				conn->cn_routorder = pkt;
			else
				pkt->pk_next->pk_next = pkt;

			pkt->pk_next = npkt;

			debugf(DBG_INFO, "rcvdata: New out of order packet");
		}
	}
}


/*
 * Receive a packet - called from receiver interrupt level.
 */
void
rcvpkt(struct chxcvr *xp)
{
	int i;
	int fwdcnt;
	struct packet *pkt = (struct packet *)xp->xc_rpkt;
	struct connection *conn;
	unsigned index;

	tracef(TRACE_LOW, "rcvpkt:");
	
	/* xp->xc_rcvd++ */
	xp->LELNG_xc_rcvd = LE_TO_LONG(LE_TO_LONG(xp->LELNG_xc_rcvd) + 1);
	pkt->pk_next = NOPKT;
	if (CH_ADDR_SHORT(xp->xc_addr)) {
		struct chroute *r;
			
		r = &chaos_routetab[xp->xc_subnet];
		r->rt_type = CHDIRECT;
		r->rt_cost = xp->xc_cost;
		r->rt_xcvr = xp;
	}

	debugf(DBG_LOW,
	       "rcvpkt: pkt->pk_daddr %o, chaos_myaddr %o, xp->xc_addr %o",
	       CH_ADDR_SHORT(pkt->pk_daddr), chaos_myaddr, 
	       CH_ADDR_SHORT(xp->xc_addr));

	if (CH_ADDR_SHORT(pkt->pk_daddr) != chaos_myaddr &&
	    CH_ADDR_SHORT(pkt->pk_daddr) != CH_ADDR_SHORT(xp->xc_addr) &&
	    CH_ADDR_SHORT(pkt->pk_daddr) != 0) {
		
 
/* JAO: forwarding doesn't make sense unless we are a bridge, and have another subnet to transmit on. 
   As it is, this simply makes a lot of noise duplicating packets on the subnet that already have a
   receiver ready to receive them. */
   
	  /* increment forwarding count in packet */
	  fwdcnt = PH_FC(pkt->pk_phead);
	  SET_PH_FC(pkt->pk_phead,fwdcnt+1);
		if (((fwdcnt+1) & 0x0f) == 0) {
			debugf(DBG_WARN, "rcvpkt: Overforwarded packet");
ignore:
			ch_free_pkt(pkt);
		} else if (CH_ADDR_SHORT(pkt->pk_saddr) == chaos_myaddr ||
			   CH_ADDR_SHORT(pkt->pk_saddr) == 
			   CH_ADDR_SHORT(xp->xc_addr)) {
			debugf(DBG_LOW, "rcvpkt: Got my own packet back");
			ch_free_pkt(pkt);
		} else if (chaos_myaddr == -1)
			ch_free_pkt(pkt);
		else {
			debugf(DBG_LOW, "rcvpkt: NOT Forwarding pkt daddr=%x", /* JAO */
			       CH_ADDR_SHORT(pkt->pk_daddr));
#if(0) /* JAO */
			sendctl(pkt);
#endif 
		}
	}
	else if (pkt->pk_op == RUTOP)
		rcvrut(pkt);
	else if (pkt->pk_op == MNTOP)
		goto ignore;
	else if (pkt->pk_op == RFCOP) {
	  debugf(DBG_LOW, "identified RFC op");
		rcvrfc(pkt);
	}
	else if (pkt->pk_op == BRDOP)
		rcvbrd(pkt);

	/*
	 * Check for various flavors of bad indexes
	 */
	else if ((index = pkt->pk_dtindex) >= CHNCONNS ||
		 ((conn = chaos_conntab[index]) == NOCONN) ||
		 CH_INDEX_SHORT(conn->cn_Lidx) != CH_INDEX_SHORT(pkt->pk_didx))
	{
		debugf(DBG_WARN, "rcvpkt: Packet with bad index: %x, op:%d",
		       CH_INDEX_SHORT(pkt->pk_didx), pkt->pk_op);
		send_los(pkt, "Connection doesn't exist");

	/*
	 * Handle responses to our RFC
	 */
	} else if (conn->cn_state == CSRFCSENT)
		switch(pkt->pk_op) {
		case OPNOP:
			debugf(DBG_INFO,
			       "rcvpkt: Conn #%x: OPEN received",
			       CH_INDEX_SHORT(conn->cn_Lidx));

			/*
			 * Make the connection open, taking his index
			 */
			conn->cn_state = CSOPEN;
			conn->cn_Fidx = pkt->pk_sidx;
			conn->cn_active = chaos_clock;
			/*
			 * Indicate we have received AND "read" the OPEN
			 * Read his window size, indicate that he has
			 * received and acked the RFC, and acknowledge the OPN
			 */
			conn->cn_rread = conn->cn_rlast = 
			  LE_TO_SHORT(pkt->LE_pk_pkn);
			conn->cn_twsize = LE_TO_SHORT(pkt->LE_pk_rwsize);
			receipt(conn, LE_TO_SHORT(pkt->LE_pk_ackn), 
				LE_TO_SHORT(pkt->LE_pk_ackn));
			/*
			 * Wakeup the user waiting for the connection to open.
			 */
			makests(conn, pkt);
			reflect(pkt);
			NEWSTATE(conn);
			break;
		case CLSOP:
		case ANSOP:
		case FWDOP:
			debugf(DBG_LOW,
			       "rcvpkt: Conn #%x: CLOSE/ANS received for RFC",
			       CH_INDEX_SHORT(conn->cn_Lidx));
			clsconn(conn, CSCLOSED, pkt);
			break;
		default:
			debugf(DBG_LOW,
			       "rcvpkt: bad packet type "
			       "for conn #%x in CSRFCSENT: %d",
			       CH_INDEX_SHORT(conn->cn_Lidx), pkt->pk_op);
			send_los(pkt, "Bad packet type reponse to RFC");
		}
	/*
	 * Process a packet for an open connection
	 */
	else if (conn->cn_state == CSOPEN) {
		debugf(DBG_LOW, "rcvpkt: conn #%x open", 
		       CH_INDEX_SHORT(conn->cn_Lidx));
		conn->cn_active = chaos_clock;
		if (ISDATOP(pkt)) {
			debugf(DBG_LOW,
			       "rcvpkt: conn #%x open, giving it data",
			       CH_INDEX_SHORT(conn->cn_Lidx));
			rcvdata(conn, pkt);
		}
		else switch (pkt->pk_op) {
	    	case OPNOP:
			/*
			 * Ignore duplicate opens.
			 */
			debugf(DBG_LOW, "rcvpkt: Duplicate open received");
			ch_free_pkt(pkt);
			break;
		case SNSOP:
			if (1) prpkt(pkt, "SNS");
			receipt(conn, LE_TO_SHORT(pkt->LE_pk_ackn), 
				LE_TO_SHORT(pkt->LE_pk_ackn));
			makests(conn, pkt);
			reflect(pkt);
			break;
	    	case EOFOP:
			rcvdata(conn, pkt);
			break;
	    	case LOSOP:
		case CLSOP:
			debugf(DBG_LOW,
			       "rcvpkt: Close rcvd on %x", 
			       CH_INDEX_SHORT(conn->cn_Lidx));
			clsconn(conn, pkt->pk_op == CLSOP ? CSCLOSED : CSLOST, pkt);
			break;
			/*
			 * Uncontrolled data - que it at the head of the rlist
			 */
	    	case UNCOP:
			if ((pkt->pk_next = conn->cn_rhead) == NOPKT)
				conn->cn_rtail = pkt;
			conn->cn_rhead = pkt;
			if (pkt == conn->cn_rtail)
				INPUT(conn);
			receipt(conn, LE_TO_SHORT(pkt->LE_pk_ackn), 
				LE_TO_SHORT(pkt->LE_pk_ackn));
			break;
	    	case STSOP:
#if 1
			prpkt(pkt, "STS");
			debugf(DBG_INFO, 
			       "Conn #%x, Receipt=%d",
			       conn->cn_Lidx,
			       LE_TO_SHORT(pkt->pk_idata[0]));
#endif
			if (LE_TO_SHORT(pkt->LE_pk_rwsize) > conn->cn_twsize)
				OUTPUT(conn);
			conn->cn_twsize = LE_TO_SHORT(pkt->LE_pk_rwsize);
			receipt(conn, LE_TO_SHORT(pkt->LE_pk_ackn), 
				LE_TO_SHORT(pkt->LE_pk_receipt));
			ch_free_pkt(pkt);
			if (conn->cn_thead != NOPKT)
				chretran(conn, CHSHORTTIME);
			break;
		default:
			debugf(DBG_LOW, "rcvpkt: bad opcode:%d", pkt->pk_op);
			send_los(pkt, "Bad opcode");
			/* should we do clsconn here? */
		}
	/*
	 * Connection is neither waiting for an OPEN nor OPEN.
	 */
	} else {
		debugf(DBG_LOW,
		       "rcvpkt: Packet for conn #%x (not open) "
		       "state=%d, op:%d",
		       CH_INDEX_SHORT(conn->cn_Lidx), 
		       conn->cn_state, pkt->pk_op);
		send_los(pkt, "Connection is closed");
	}
	debugf(DBG_LOW, "rcvpkt: at end");
}


/* ------------------------------------------------------------------------- */

#define E2BIG	10
#define ENOBUFS	11
#define ENXIO	12
#define EIO	13

/*
 * Return a connection or return NULL and set *errnop to any error.
 */
struct connection *
chopen(struct chopen *c, int wflag,int *errnop)
{
	struct connection *conn;
	struct packet *pkt;
	int rwsize, length;
        struct chopen cho;

        tracef(TRACE_LOW, "chopen(wflag=%d)", wflag);

	length = c->co_clength + c->co_length + (c->co_length ? 1 : 0);
	if (length > CHMAXPKT ||
	    c->co_clength <= 0) {
		*errnop = -E2BIG;
		return NOCONN;
	}

	debugf(DBG_LOW,
	       "chopen: c->co_length %d, c->co_clength %d, length %d",
	       c->co_length, c->co_clength, length);

	pkt = ch_alloc_pkt(length);
	if (pkt == NOPKT) {
		*errnop = -ENOBUFS;
		return NOCONN;
	}

	if (c->co_length)
		pkt->pk_cdata[c->co_clength] = ' ';

	memcpy(pkt->pk_cdata, c->co_contact, c->co_clength);
	if (c->co_length)
		memcpy(&pkt->pk_cdata[c->co_clength + 1], c->co_data,
		       c->co_length);

	rwsize = c->co_rwsize ? c->co_rwsize : CHDRWSIZE;
	SET_PH_LEN(pkt->pk_phead,length);

	conn = c->co_host ?
		ch_open(c->co_host, rwsize, pkt) :
		ch_listen(pkt, rwsize);

	if (conn == NOCONN) {
		debugf(DBG_LOW, "chopen: NOCONN");
		*errnop = -ENXIO;
		return NOCONN;
	}

	debugf(DBG_LOW, "chopen: c->co_async %d", c->co_async);
	debugf(DBG_LOW, "chopen: conn %p", conn);

#if 0
	if (!c->co_async) {
		/*
		 * We should hang until the connection changes from
		 * its initial state.
		 * If interrupted, flush the connection.
		 */

//		current->timeout = (unsigned long) -1;

		*errnop = chwaitfornotstate(conn, c->co_host ?
                                            CSRFCSENT : CSLISTEN);
		if (*errnop) {
			rlsconn(conn);
			return NOCONN;
		}

		/*
		 * If the connection is not open, the open failed.
		 * Unless is got an ANS back.
		 */
		if (conn->cn_state != CSOPEN &&
		    (conn->cn_state != CSCLOSED ||
		     (pkt = conn->cn_rhead) == NOPKT ||
		     pkt->pk_op != ANSOP))
		{
			debugf(DBG_LOW,
			       "chopen: open failed; cn_state %d",
			       conn->cn_state);
			rlsconn(conn);
			*errnop = -EIO;
			return NOCONN;
		}
	}
#endif

//	if (wflag)
//		conn->cn_sflags |= CHFWRITE;
//	conn->cn_sflags |= CHRAW;

	conn->cn_mode = CHSTREAM;
        debugf(DBG_LOW, "chopen() done");

	return conn;
}

struct chxcvr intf;

void
ch_rcv_pkt_buffer(char *buffer, int size)
{
	struct packet *pkt;
	struct chxcvr *xp;

	pkt = ch_alloc_pkt(size);
	if (pkt == 0)
		return;

	memcpy((char *)&pkt->pk_phead, buffer, size); 
	intf.xc_rpkt = pkt;

	rcvpkt(&intf);
}

extern int chaos_xmit(struct chxcvr *intf, struct packet *pkt, int at_head_p);

int
chaos_init(void)
{
	chaos_myaddr = 0x0104;
	strcpy(chaos_myname, "server");

	chaos_rfcrcv = 1;

	intf.xc_xmit = chaos_xmit;
	SET_CH_ADDR(intf.xc_addr,chaos_myaddr);

	chaos_routetab[1].rt_type = 0;

#if 1
	/* pick random uniq so a restarted server doesn't reuse old ids */
	srand(time(0L));
	uniq = rand();
#endif

	return 0;
}
