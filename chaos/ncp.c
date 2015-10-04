/*
 * ncp.c
 * userspace NCP protocol implementation
 *
 * original; Brad Parker <brad@heeltoe.com>
 * byte order cleanups; Joseph Oswald <josephoswald@gmail.com>
 *
 * $Id: ncp.c 78 2006-07-18 18:28:34Z brad $
 */

#include "chaos.h"
#include "server.h"
#include "ncp.h"
#include "log.h"

extern int chaos_myaddr;
extern int chaos_clock;
extern struct packet *chaos_rfclist;
extern struct packet *chaos_rfctail;
extern struct packet *chaos_lsnlist;

static struct packet *rfcseen;	/* used by ch_rnext and ch_listen */

struct packet *ch_alloc_pkt(int datasize);
void ch_close(struct connection *conn, struct packet *pkt, int release);
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

/*
 * Set packet fields from connection, many routines count on the fact that
 * this routine clears pk_type and pk_next
 */
void
setpkt(struct connection *conn, struct packet *pkt)
{
	pkt->pk_daddr = conn->cn_faddr;
	pkt->pk_didx = conn->cn_Fidx;
	pkt->pk_saddr = conn->cn_laddr;
	pkt->pk_sidx = conn->cn_Lidx;
	pkt->pk_type = 0;
	pkt->pk_next = NOPKT;
	SET_PH_FC(pkt->pk_phead,0);
}

int
ch_full(struct connection *conn)
{
    return chtfull(conn);
}

int
ch_empty(struct connection *conn)
{
    return chtempty(conn);
}

/* ------------------------------------------------------------------------ */

/*
 * Send a new data packet on a connection.
 * Called at high priority since window size check is elsewhere.
 */
int
ch_write(struct connection *conn, struct packet *pkt)
{
	tracef(TRACE_MED, "ch_write(pkt=%x)\n", pkt);

	switch (pkt->pk_op) {
	case ANSOP:
	case FWDOP:
		if (conn->cn_state != CSRFCRCVD ||
		    (conn->cn_flags & CHANSWER) == 0)
			goto err;
		ch_close(conn, pkt, 0);
		return 0;
	case RFCOP:
	case BRDOP:
		if (conn->cn_state != CSRFCSENT)
			goto err;
		break;
	case UNCOP:
		pkt->LE_pk_pkn = 0;
		setpkt(conn, pkt);
		senddata(pkt);
		return 0;
	default:
		if (!ISDATOP(pkt))
			goto err;
	case OPNOP:
	case EOFOP:
		if (conn->cn_state != CSOPEN)
			goto err;
		setack(conn, pkt);
		break;
	}
	setpkt(conn, pkt);
        ++conn->cn_tlast;
	pkt->LE_pk_pkn = LE_TO_SHORT(conn->cn_tlast);
	senddata(pkt);
	debugf(DBG_LOW, "ch_write: done");
	return 0;
err:
	ch_free_pkt(pkt);
	debugf(DBG_LOW, "ch_write: error");
	return CHERROR;
}

/*
 * Consume the packet at the head of the received packet queue (rhead).
 * Assumes high priority because check for available is elsewhere
 */
void
ch_read(struct connection *conn)
{
	struct packet *pkt;

        tracef(TRACE_MED, "ch_read:\n");
	if ((pkt = conn->cn_rhead) == NOPKT)
		return;
	conn->cn_rhead = pkt->pk_next;
	if (conn->cn_rtail == pkt)
		conn->cn_rtail = NOPKT;
	if (CONTPKT(pkt)) {
		conn->cn_rread = LE_TO_SHORT(pkt->LE_pk_pkn);
		if (pkt->pk_op == EOFOP ||
		    3 * (short)(conn->cn_rread - conn->cn_racked) > conn->cn_rwsize) {

			debugf(DBG_LOW,
                               "ch_read: Conn#%x: rread=%d rackd=%d rsts=%d\n",
			       conn->cn_Lidx, conn->cn_rread,
			       conn->cn_racked, conn->cn_rsts);

			pkt->pk_next = NOPKT;
			makests(conn, pkt);
			reflect(pkt);
			return;
		}
	}

	ch_free_pkt(pkt);
}

/*
 * Open a connection (send a RFC) given a destination host a RFC
 * packet, and a default receive window size.
 * Return the connection, on which the RFC has been sent.
 * The connection is not necessarily open at this point.
 */
struct connection *
ch_open(int destaddr, int rwsize, struct packet *pkt)
{
	struct connection *conn;

	tracef(TRACE_MED, "ch_open:\n");

	if ((conn = allconn()) == NOCONN) {
		ch_free_pkt(pkt);
		return(NOCONN);
	}
	/*
	 * This could be optimized to use the local address which has
	 * an active transmitter which is on the same subnet as the
	 * destination subnet.  This would involve knowing all the
	 * local addresses - which we don't currently maintain in a
	 * convenient form.
	 */
	SET_CH_ADDR(conn->cn_laddr,chaos_myaddr);
	SET_CH_ADDR(conn->cn_faddr,destaddr);
	conn->cn_state = CSRFCSENT;
	SET_CH_INDEX(conn->cn_Fidx,0);
	conn->cn_rwsize = rwsize;
	conn->cn_rsts = rwsize / 2;
	conn->cn_active = chaos_clock;
	pkt->pk_op = RFCOP;
	SET_PH_FC(pkt->pk_phead,0);
	pkt->LE_pk_ackn = 0;

	debugf(DBG_LOW, "ch_open: Conn #%x: RFCS state\n", 
               CH_INDEX_SHORT(conn->cn_Lidx));

	/*
	 * By making the RFC packet written like a data packet,
	 * it will be freed by the normal receipting mechanism, enabling
	 * to easily be kept around for automatic retransmission.
	 * xmitdone (first half) and rcvpkt (handling OPEN pkts) help here.
	 * Since allconn clears the connection (including cn_tlast) the
	 * packet number of the RFC will be 1 (ch_write does pkn = ++tlast)
	 */

	ch_write(conn, pkt);	/* No errors possible */

	debugf(DBG_LOW, "ch_open: done\n");
	return(conn);
}

/*
 * Start a listener, given a packet with the contact name in it.
 * In all cases packet is consumed.
 * Connection returned is in the listen state.
 */
struct connection *
ch_listen(struct packet *pkt, int rwsize)
{
	struct connection *conn;
	struct packet *pktl, *opkt;

        tracef(TRACE_MED, "ch_listen()");

	if ((conn = allconn()) == NOCONN) {
		ch_free_pkt(pkt);
		return(NOCONN);
	}

	conn->cn_state = CSLISTEN;
	conn->cn_rwsize = rwsize;
	pkt->pk_op = LSNOP;
	setpkt(conn, pkt);

	opkt = NOPKT;
	for (pktl = chaos_rfclist; pktl != NOPKT; pktl = (opkt = pktl)->pk_next)
		if (concmp(pktl, pkt->pk_cdata, (int)(PH_LEN(pkt->pk_phead)))) {
			if(opkt == NOPKT)
				chaos_rfclist = pktl->pk_next;
			else
				opkt->pk_next = pktl->pk_next;

			if (pktl == chaos_rfctail)
				chaos_rfctail = opkt;
			if (pktl == rfcseen)
				rfcseen = NOPKT;

			ch_free_pkt(pkt);
			lsnmatch(pktl, conn);

			return(conn);
		}
	/*
	 * Should we check for duplicate listeners??
	 * Or is it better to allow more than one?
	 */
	pkt->pk_next = chaos_lsnlist;
	chaos_lsnlist = pkt;
	debugf(DBG_LOW, "ch_listen: Conn #%x: LISTEN state\n", 
               CH_INDEX_SHORT(conn->cn_Lidx));

	return(conn);
}

/*
 * Accept an RFC, called on a connection in the CSRFCRCVD state.
 */
void
ch_accept(struct connection *conn)
{
	struct packet *pkt = ch_alloc_pkt(sizeof(struct sts_data));

	tracef(TRACE_MED, "ch_accept: state %o, want %o\n",
               conn->cn_state, CSRFCRCVD);

	if (conn->cn_state != CSRFCRCVD)
		ch_free_pkt(pkt);
	else {
		if (conn->cn_rhead != NOPKT && conn->cn_rhead->pk_op == RFCOP)
			 ch_read(conn);
		conn->cn_state = CSOPEN;
		conn->cn_tlast = 0;
		conn->cn_rsts = conn->cn_rwsize >> 1;
		pkt->pk_op = OPNOP;
		SET_PH_LEN(pkt->pk_phead,sizeof(struct sts_data));
		pkt->LE_pk_receipt = LE_TO_SHORT(conn->cn_rlast);
		pkt->LE_pk_rwsize = LE_TO_SHORT(conn->cn_rwsize);

		debugf(DBG_LOW, "ch_accept: Conn #%o: open sent\n",
                       CH_INDEX_SHORT(conn->cn_Lidx));

		ch_write(conn, pkt);
	}
}

/*
 * Close a connection, giving close pkt to send (CLS or ANS).
 */
void
ch_close(struct connection *conn, struct packet *pkt, int release)
{
        tracef(TRACE_MED, "ch_close()\n");

	switch (conn->cn_state) {
	    case CSOPEN:
	    case CSRFCRCVD:
		if (pkt != NOPKT) {
			setpkt(conn, pkt);
			pkt->LE_pk_ackn = pkt->LE_pk_pkn = 0;
			sendctl(pkt);
			pkt = NOPKT;
		}
		/* Fall into... */
	    case CSRFCSENT:
		clsconn(conn, CSCLOSED, NOPKT);
		break;
	    case CSLISTEN:
		rmlisten(conn);
		break;
	    default:
		break;
	}
	if (pkt)
		ch_free_pkt(pkt);

	if (release)
		rlsconn(conn);
}

int
ch_setmode(struct connection *conn, int mode)
{
    int ret = 0;

    switch (mode) {
    case CHTTY:
        ret = -1;
        break;
    case CHSTREAM:
    case CHRECORD:
        if (conn->cn_mode == CHTTY)
            ret = -1;
        else
            conn->cn_mode = mode;
    }

    return ret;
}



/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/

