/*
 * clock.c
 * $Id: clock.c 46 2005-10-01 12:02:45Z brad $
 */

#include <stdio.h>
#include <unistd.h>

#include "chaos.h"
#include "ncp.h"
#include "server.h"

/*
 * Clock level processing.
 *	ch_clock should be called each clock tick (HZ per second)
 *	at a priority equal to or higher that LOCK.
 *
 * Terminology:
 *    Packet aging:	Retransmitting packets that are not acked within
 *			AGERATE ticks
 *
 *    Probe:		The sending of a SNS packet if not all of the packets
 *			we have sent have been acknowledged
 *
 *    Responding:	Send a SNS every so often to see if the guy is still
 *			alive (after NRESPONDS we declare him dead)
 *
 *    RFC aging:	The retransmission of RFC packets
 *
 *    Route aging:	Increase the cost of transmitting over gateways so we
 *			will use another gateway if the current gateway goes
 *			down.
 *    Route broadcast:	If we are connected to more than one subnet, broad
 *			cast our bridge status every BRIDGERATE seconds.
 *
 *    Interface hung:	Checking periodically for dead interfaces (or dead
 *			"other end"'s of point-to-point links).
 *
 * These rates might want to vary with the cost of getting to the host.
 * They also might want to reside in the chconf.h file if they are not a real
 * network standard.
 *
 * Since these rates are dependent on a run-time variable
 * (This is a good idea if you think about it long enough),
 * We might want to initialize specific variables at run-time to
 * avoid recalculation if the profile of chclock is disturbing.
 */
#define MINRATE		ORATE		/* Minimum of following rates */
#define HANGRATE	(500)		/* How often to check for hung
					   interfaces */
#define AGERATE		(1000)		/* Re-xmit pkt if not rcptd in time */
#define PROBERATE	(8*1000)	/* Send SNS to get STS for receipts or
					   to make sure the conn. is alive */
#define ORATE		(500)		/* Xmit current (stream) output packet
					   if not touched in this time */
#define TESTRATE	(45*1000)	/* Respond testing rate */
#define ROUTERATE	(4*1000)	/* Route aging rate */
#define BRIDGERATE	(15*1000)	/* Routing broadcast rate */
#define NRESPONDS	3		/* Test this many times before timing
					   out the connection */
#define UPTIME  	(NRESPONDS*TESTRATE)	/* Nothing in this time and
					   the connection is flushed */
#define RFCTIME		(15*1000)	/* Try CHRFCTRYS times to RFC */

extern int chaos_clock;
extern int chaos_nobridge;

extern struct chroute chaos_routetab[];

void
ch_clock(void)
{
	struct connection *conn;
	struct connection **connp;
	struct packet *pkt;
	chtime inactive;
	int probing;			/* are we probing this time ? */
	static chtime nextclk = 1;	/* next time to do anything */
	static chtime nextprobe = 1;	/* next time to probe */
	static chtime nexthang = 1;	/* next time to chxtime() */
	static chtime nextroute = 1;	/* next time to age routing */
	static chtime nextbridge = 1;	/* next time to send routing */

	if (nextclk != ++chaos_clock)
		return;
	nextclk += MINRATE;
	if (cmp_gt(chaos_clock, nextprobe)) {
		probing = 1;
		nextprobe += PROBERATE;
	} else
		probing = 0;
	if (cmp_gt(chaos_clock, nexthang)) {
		chxtime();
		nexthang += HANGRATE;
	}
	if (cmp_gt(chaos_clock, nextroute)) {
		chroutage();
		nextroute += ROUTERATE;
	}
	if (cmp_gt(chaos_clock, nextbridge)) {
		chbridge();
		nextbridge += BRIDGERATE;
	}
#if 0
	debug(DNOCLK,return);
#endif
	for (connp = &Chconntab[0]; connp < &Chconntab[CHNCONNS]; connp++) {
		if ((conn = *connp) == NOCONN ||
		    (conn->cn_state != CSOPEN &&
		     conn->cn_state != CSRFCSENT))
			continue;
#ifdef CHSTRCODE
		if ((pkt = conn->cn_toutput) != NOPKT &&
		    cmp_gt(chaos_clock, pkt->pk_time + ORATE) &&
		    !chtfull(conn)) {
			conn->cn_toutput = NOPKT;
			ch_write(conn, pkt);
		}
#endif
		if (conn->cn_thead != NOPKT)
			chretran(conn, AGERATE);
		if (probing) {
			inactive = chaos_clock - conn->cn_active;
			if (inactive >= (conn->cn_state == CSOPEN ?
					 UPTIME : RFCTIME)) {
				printf("ch_clock: Conn #%x: Timeout\n",
				       conn->cn_lidx);
				clsconn(conn, CSINCT, NOPKT);
			} else if (conn->cn_state == CSOPEN &&
				   (conn->cn_tacked != conn->cn_tlast ||
				    chtfull(conn) ||
				    inactive >= TESTRATE)) {
				printf("ch_clock: Conn #%x: Probe: %ld\n",
				       conn->cn_lidx, inactive);
				sendsns(conn);
			}
		}
	}
}

/*
 * Increase the cost of accessing a subnet via a gateway
 */
void
chroutage(void)
{
	struct chroute *r;

	for (r = chaos_routetab; r < &chaos_routetab[CHNSUBNET]; r++)
		if ((r->rt_type == CHBRIDGE || r->rt_type == CHDIRECT) &&
		    r->rt_cost < CHHCOST)
			r->rt_cost++;
}
/*
 * Send routing packets on all directly connected subnets, unless we are on
 * only one.
 */
chbridge()
{
	register struct chroute *r;
	register struct packet *pkt;
	register struct rut_data *rd;
	register int ndirect;
	register int n;

	if (chaos_nobridge)
		return;
	/*
	 * Count the number of subnets to which we are directly connected.
	 * If not more than one, then we are not a bridge and shouldn't
	 * send out routing packets at all.
	 * While we're at it, count the number of subnets we know we
	 * have any access to.  This number determines the size of the
	 * routine packet we need to send, if any.
	 */
	n = ndirect = 0;
	for (r = chaos_routetab; r < &chaos_routetab[CHNSUBNET]; r++)
		if (r->rt_cost < CHHCOST)
			switch(r->rt_type) {
			case CHDIRECT:
				ndirect++;
			default:
				n++;
				break;
			case CHNOPATH:
				;	
			}

	if (ndirect <= 1 ||
	    (pkt = pkalloc(n * sizeof(struct rut_data), 1)) == NOPKT)
		return;
	/*
	 * Build the routing packet to send out on each directly connected
	 * subnet.  It is complete except for the cost of the directly
	 * connected subnet we are sending it out on.  This cost must be
	 * added to each entry in the packet each time it is sent.
	 */
	pkt->pk_len = n * sizeof(struct rut_data);
	pkt->pk_op = RUTOP;
	pkt->pk_type = pkt->pk_daddr = pkt->pk_sidx = pkt->pk_didx = 0;
	pkt->pk_next = NOPKT;
	rd = pkt->pk_rutdata;
	for (n = 0, r = chaos_routetab; r < &chaos_routetab[CHNSUBNET]; r++, n++)
		if (r->rt_cost < CHHCOST && r->rt_type != CHNOPATH) {
			rd->pk_subnet = n;
			rd->pk_cost = r->rt_cost;
			rd++;
		}
	/*
	 * Now send out this packet on each directly connected subnet.
	 * ndirect becomes zero on last such subnet.
	 */
	for (r = chaos_routetab; r < &chaos_routetab[CHNSUBNET]; r++)
		if (r->rt_type == CHDIRECT && r->rt_cost < CHHCOST)
			sendrut(pkt, r->rt_xcvr, r->rt_cost, --ndirect);
}
