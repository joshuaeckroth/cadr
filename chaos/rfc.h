#ifndef RFC_H
#define RFC_H

#include <stdio.h>
#include <unistd.h>

#include <sys/time.h>


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

void statusrfc(struct packet *pkt);
void timerfc(struct packet *pkt);
void uptimerfc(struct packet *pkt);
void dumprtrfc(struct packet *pkt);




#endif
