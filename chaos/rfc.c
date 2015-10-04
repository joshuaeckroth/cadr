/*
 * rfc.c
 *
 * original; Brad Parker <brad@heeltoe.com>
 * byte order cleanups; Joseph Oswald <josephoswald@gmail.com>
 *
 * $Id: rfc.c 78 2006-07-18 18:28:34Z brad $
 */

#include <stdio.h>
#include <unistd.h>

#include <sys/time.h>

#include "chaos.h"
#include "ncp.h"
#include "server.h"
#include "log.h"
#include "rfc.h"

extern struct chroute chaos_routetab[];
extern char chaos_myname[];

/*
 * process a RFC for contact name STATUS
 */
void
statusrfc(struct packet *pkt)
{
	struct chroute *r;
	struct chxcvr *xp;
	struct statdata *sp;
	int i;
	int saddr = CH_ADDR_SHORT(pkt->pk_saddr);
	int sidx = CH_INDEX_SHORT(pkt->pk_sidx);
	int daddr = CH_ADDR_SHORT(pkt->pk_daddr);
	
	debugf(DBG_LOW, "statusrfc:");

	ch_free_pkt(pkt);
	for (i = 0, r = chaos_routetab; r < &chaos_routetab[CHNSUBNET]; r++)
		if (r->rt_type == CHDIRECT)
			i++;
	i *= sizeof(struct stathead) + sizeof(struct statxcvr);
	i += CHSTATNAME;
	if ((pkt = ch_alloc_pkt(i)) == NOPKT)
		return;
	SET_CH_ADDR(pkt->pk_daddr,saddr);
	SET_CH_INDEX(pkt->pk_didx,sidx);
	pkt->pk_type = 0;
	pkt->pk_op = ANSOP;
	pkt->pk_next = NOPKT;
	SET_CH_ADDR(pkt->pk_saddr,daddr);
	SET_CH_INDEX(pkt->pk_sidx,0);
	pkt->LE_pk_pkn = pkt->LE_pk_ackn = 0;
	SET_PH_LEN(pkt->pk_phead,i); 	  /* pkt->pk_lenword = i; */

	chmove(chaos_myname, pkt->pk_status.sb_name, CHSTATNAME);
	sp = &pkt->pk_status.sb_data[0];
	for (r = chaos_routetab; r < &chaos_routetab[CHNSUBNET]; r++) {
		if (r->rt_type == CHDIRECT) {
			xp = r->rt_xcvr;
			sp->LE_sb_ident = LE_TO_SHORT(0400 + xp->xc_subnet);
			sp->LE_sb_nshorts = 
			  LE_TO_SHORT(sizeof(struct statxcvr) / sizeof(short));
			sp->sb_xstat = xp->xc_xstat;
			sp = (struct statdata *)((char *)sp +
				sizeof(struct stathead) +
				sizeof(struct statxcvr));
		}
	}

	debugf(DBG_LOW, "statusrfc: answering");

	sendctl(pkt);
}

/*
 * Return the time according to the chaos TIME protocol, in a long.
 * No byte shuffling need be done here, just time conversion.
 */
void
ch_time(long *tp)
{
	struct timeval time;

	gettimeofday(&time, NULL);

	*tp = time.tv_sec;
	*tp += 60UL*60*24*((1970-1900)*365L + 1970/4 - 1900/4);
}

void
ch_uptime(long *tp)
{
//	*tp = jiffies;
	*tp = 0;
}

/*
 * process a RFC for contact name TIME 
 */
void
timerfc(struct packet *pkt)
{
	long t;

	debugf(DBG_LOW, "timerfc: answering");

	pkt->pk_op = ANSOP;
	pkt->pk_next = NOPKT;
	pkt->LE_pk_pkn = pkt->LE_pk_ackn = 0;
	SET_PH_LEN(pkt->pk_phead, sizeof(long));
	ch_time(&t);
	pkt->pk_ldata[0] = LE_TO_LONG(t);
	reflect(pkt);
}

void
uptimerfc(struct packet *pkt)
{
	long t;

	pkt->pk_op = ANSOP;
	pkt->pk_next = NOPKT;
	pkt->LE_pk_pkn = pkt->LE_pk_ackn = 0;
	SET_PH_LEN(pkt->pk_phead, sizeof(long));
	ch_uptime(&t);
	pkt->pk_ldata[0] = LE_TO_LONG(t);
	reflect(pkt);
}

void
dumprtrfc(struct packet *pkt)
{
	struct chroute *r;
	short *wp;
	int ndirect, i;
	int saddr = CH_ADDR_SHORT(pkt->pk_saddr);
	int sidx = CH_INDEX_SHORT(pkt->pk_sidx);
	int daddr = CH_ADDR_SHORT(pkt->pk_daddr);
	
	ch_free_pkt(pkt);
	if ((pkt = ch_alloc_pkt(CHNSUBNET * 4)) != NOPKT) {
		wp = pkt->pk_idata;
		ndirect = i = 0;
		for (r = chaos_routetab; r < &chaos_routetab[CHNSUBNET]; r++, i++)
			if (r->rt_type == CHDIRECT) {
				*wp++ = (ndirect++ << 1) + 1;
				*wp++ = i;
			} else {
				*wp++ = CH_ADDR_SHORT(r->rt_addr);
				*wp++ = r->rt_cost;
			}
		SET_CH_ADDR(pkt->pk_daddr,saddr);
		SET_CH_INDEX(pkt->pk_didx,sidx);
		pkt->pk_type = 0;
		pkt->pk_op = ANSOP;
		pkt->pk_next = NOPKT;
		SET_CH_ADDR(pkt->pk_saddr,daddr);
		SET_CH_INDEX(pkt->pk_sidx,0);
		pkt->LE_pk_pkn = pkt->LE_pk_ackn = 0;
		SET_PH_LEN(pkt->pk_phead,CHNSUBNET * 4);
		sendctl(pkt);
	}
}
