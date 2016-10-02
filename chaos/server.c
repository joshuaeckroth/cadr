/*
 * server.c
 * chaosnet protocol server
 *
 * chaos node with chaos protocol processing
 * connects to chaosd server
 * forks servers and does protocol procesing (NCP, etc...)
 *
 * original; Brad Parker <brad@heeltoe.com>
 * byte order cleanups; Joseph Oswald <josephoswald@gmail.com>
 * 
 * $Id: server.c 79 2006-07-18 20:51:05Z brad $
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/uio.h>

#include "chaos.h"
#include "ncp.h"
#include "server.h"
#include "log.h"
#include "chaosd.h"

#define SERVER_VERSION 003

int flag_daemon;
int flag_debug_level;
int flag_debug_time;
int flag_debug_level;
int flag_trace_level;

int server_running;
int fd;
struct sockaddr_un unix_addr;
u_char buffer[4096];
u_char *msg, resp[8];

char rcs_id[] = "$Id: server.c 79 2006-07-18 20:51:05Z brad $";

extern int chaos_myaddr;

struct {
    unsigned long rx;
    unsigned long tx;
} stats;


void
dumpbuffer(u_char *buf, int cnt)
{
    int i, j, offset, skipping;
    char cbuf[17];
    char line[80];
	
    offset = 0;
    skipping = 0;
    while (cnt > 0) {
        if (offset > 0 && memcmp(buf, buf-16, 16) == 0) {
            skipping = 1;
        } else {
            if (skipping) {
                skipping = 0;
                printf("...\n");
            }
        }

        if (!skipping) {
            for (j = 0; j < 16; j++) {
                char *pl = line+j*3;
				
                if (j >= cnt) {
                    strcpy(pl, "xx ");
                    cbuf[j] = 'x';
                } else {
                    sprintf(pl, "%02x ", buf[j]);
                    cbuf[j] = buf[j] < ' ' ||
                        buf[j] > '~' ? '.' : buf[j];
                }
                pl[3] = 0;
            }
            cbuf[16] = 0;

            printf("%08x %s %s\n", offset, line, cbuf);
        }

        buf += 16;
        cnt -= 16;
        offset += 16;
    }

    if (skipping) {
        skipping = 0;
        printf("%08x ...\n", offset-16);
    }
}

/*
 * connect to chaosd network server using specificed socket type
 */
int
connect_to_server(void)
{
    int len;

    tracef(TRACE_HIGH, "connect_to_server()");

    if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
      perror("socket(AF_UNIX)");
      return -1;
    }

    memset(&unix_addr, 0, sizeof(unix_addr));

    sprintf(unix_addr.sun_path, "%s%s%05u",
	    UNIX_SOCKET_PATH, UNIX_SOCKET_CLIENT_NAME, getpid());

    unix_addr.sun_family = AF_UNIX;
    len = SUN_LEN(&unix_addr);

    unlink(unix_addr.sun_path);

    if ((bind(fd, (struct sockaddr *)&unix_addr, len) < 0)) {
      perror("bind(AF_UNIX)");
      return -1;
    }

    if (chmod(unix_addr.sun_path, UNIX_SOCKET_PERM) < 0) {
      perror("chmod(AF_UNIX)");
      return -1;
    }

    memset(&unix_addr, 0, sizeof(unix_addr));
    sprintf(unix_addr.sun_path, "%s%s",
	    UNIX_SOCKET_PATH, UNIX_SOCKET_SERVER_NAME);
    unix_addr.sun_family = AF_UNIX;
    len = SUN_LEN(&unix_addr);

    if (connect(fd, (struct sockaddr *)&unix_addr, len) < 0) {
      perror("connect(AF_UNIX)");
      return -1;
    }

    debugf(DBG_LOW, "fd %d", fd);
        
    return 0;
}

static int pkt_num;

/*
 * acting like an ethernet driver, transmit a packet
 * to the chaosd server to be sent on the "wire"
 */
int
chaos_xmit(struct chxcvr *intf,
	   struct packet *pkt,
	   int at_head_p)
{
	int chlength = PH_LEN(pkt->pk_phead) + sizeof(struct pkt_header);
	char *ptr = (char *)&pkt->pk_phead;

	struct iovec iov[3];
	u_short LE_t[3];
	unsigned char lenbytes[4];
	int ret, plen;

	debugf(DBG_INFO, "chaos_xmit(len=%d) fd %d", chlength, fd);

        /* pad odd lengths */
        if (chlength & 1)
            chlength++;

	plen = chlength + 6;

	/* chaosd header BIG ENDIAN? */
	lenbytes[0] = plen >> 8;
	lenbytes[1] = plen;
	lenbytes[2] = 0;
	lenbytes[3] = 0;
#if 1
        /* for debugging */
	lenbytes[3] = ++pkt_num;
#endif

	/*
         * network header (at end of pkt)
         * note: needs to be in network byte order
         */
	LE_t[0] = LE_TO_SHORT(CH_ADDR_SHORT(pkt->pk_phead.ph_daddr));
	LE_t[1] = LE_TO_SHORT(CH_ADDR_SHORT(pkt->pk_phead.ph_saddr));
	LE_t[2] = 0;

	iov[0].iov_base = lenbytes;
	iov[0].iov_len = 4;

	iov[1].iov_base = ptr;
	iov[1].iov_len = chlength;

	iov[2].iov_base = LE_t;
	iov[2].iov_len = 6;

	stats.tx++;
	ret = writev(fd, iov, 3);
	if (ret <  0) {
		perror("writev");
		return -1;
	}

#if 1
        if (flag_debug_level > 2)
	{
		u_char b[1024], *p;
		int i;
		p = b;
		for (i = 0; i < 3; i++) {
			memcpy(p, iov[i].iov_base, iov[i].iov_len);
			p += iov[i].iov_len;
		}
		dumpbuffer(b, p - b);
		printf("\n");
	}
#endif

        xmitdone(pkt);

	return 0;
}

/* turn packet opcode into text string */
char *popcode_to_text(int pt)
{
    switch (pt) {
    case RFCOP: return "RFC";
    case OPNOP: return "OPN";
    case CLSOP: return "CLS";
    case FWDOP: return "FWD";
    case ANSOP: return "ANS";
    case SNSOP: return "SNS";
    case STSOP: return "STS";
    case RUTOP: return "RUT";
    case LOSOP: return "LOS";
    case LSNOP: return "LSN";
    case MNTOP: return "MNT";
    case EOFOP: return "EOF";
    case UNCOP: return "UNC";
    case BRDOP: return "BRD";
    default: return "???";
    }
}

/*
 * child process
 *   connection 0..  main rfc pipe
 *     has 2 fd's (one/direction) <--> chaos ncp connection
 *     dgram control fd - for control messages
 *     stream control fd - for passing fd's to ncp connections
 *   connection 1..  create with chopen
 *     has 1 fd's, bidir <--> chaos ncp connection
 *   connection 2..  create with chopen
 *     has 1 fd's, bidir <--> chaos ncp connection
 *   ...
 *
 */

/*
 * this fork code is a hack; it needs to handle lots of
 * servers but right now we're just supporting FILE
 */
int child_pid;
int child_fd_data_in;
int child_fd_ctl;
int child_fd_sctl;
#define MAX_CHILD_CONN	10
struct {
    void *conn;
    int fd_out;
    int fd_in;
} child_conn[MAX_CHILD_CONN];
int child_conn_count;

void
fork_file(char *arg)
{
    int ret, r, i;
    int svdo[2], svdi[2], svc[2], svs[2];
    int tmp[2];

#define app_name "/bin/FILE"

    tracef(TRACE_MED, "fork_file('%s')\n", arg);

    ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, tmp);

    /* create a pair of packet based local sockets */
    ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, svdo);
    if (ret) {
        perror("socketpair");
        return;
    }

    ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, svdi);
    if (ret) {
        perror("socketpair");
        return;
    }

    ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, svc);
    if (ret) {
        perror("socketpair");
        return;
    }

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, svs);
    if (ret) {
        perror("socketpair");
        return;
    }

    close(tmp[0]);
    close(tmp[1]);

    debugf(DBG_LOW, "tmp %d %d\n", tmp[0], tmp[1]);

    /* make a copy of ourselves */
    if ((r = fork()) != 0) {
	    child_pid = r;
            debugf(DBG_LOW,"child pid is %d\n",child_pid);
            child_conn[0].fd_out = svdo[0];

            child_fd_data_in = svdi[0];
            child_conn[0].fd_in = svdi[0];

            child_fd_ctl = svc[0];
            child_fd_sctl = svs[0];
            debugf(DBG_LOW,
                   "fork_file() pid %d, fd_o %d, fd_i %d, "
                   "fd_ctl %d, fd_sctl %d\n",
                   child_pid,
                   child_conn[0].fd_out, child_conn[0].fd_in,
                   child_fd_ctl, child_fd_sctl);
	    return;
    }

    if (r == -1) {
        perror("fork");
        write_log(LOG_WARNING, "unable to fork new process; %%m");
        return;
    }

    /* we're the child */

    /* close stdin, out, err, etc... */
    close(0);
    close(1);
    close(2);
    close(3);
    close(4);

    /* and repoen as pipes */
    dup2(svdo[1], 0); /* stdin */
    dup2(svdi[1], 1);
    dup2(svdo[1], 2);
    dup2(svc[1], 3);
    dup2(svs[1], 4);

    for (i = 5; i < 256; i++) {
        close(i);
    }

    /* exec the application */
//xxx - hack - arg should come from parsing packet
    r = execl(app_name, app_name, "1", 0);

    if (r) {
        write_log(LOG_WARNING, "can't exec %s; %%m", app_name);
    }

    /* should not get here unless app not executable */
    exit(1);
}

/*
 * send data to a captive server (like FILE)
 */
void
server_input(struct connection *conn)
{
    struct packet *pkt;
    char *ptr;
    int i, conn_fd, len;

    /* we should store a hint in the conn */
    conn_fd = -1;
    for (i = 0; i < child_conn_count; i++) {
        if (child_conn[i].conn == conn) {
            conn_fd = child_conn[i].fd_out;
            debugf(DBG_LOW, "server_input: found conn %p @ %d, fd %d\n",
                   conn, i, conn_fd);
            break;
        }
    }

    if (conn_fd < 0) {
        debugf(DBG_WARN, "server_input: NO CONNECTION?\n");
        return;
    }

    while ((pkt = conn->cn_rhead) != NOPKT) {

	int chlength = PH_LEN(pkt->pk_phead) + sizeof(struct pkt_header);
	char *ptr = (char *)&pkt->pk_phead;

        /* show data */
        debugf(DBG_LOW, "server_input: pkt %d, data %d bytes\n",
               chlength, PH_LEN(pkt->pk_phead));
        if (flag_debug_level > 5) {
            dumpbuffer((u_char *)pkt->pk_cdata, PH_LEN(pkt->pk_phead));
        }

        if (conn_fd) {
            ptr = pkt->pk_cdata;
            len = PH_LEN(pkt->pk_phead);

            ptr--;
            ptr[0] = pkt->pk_op;
            len++;

            write(conn_fd, ptr, len);
        }

        /* consume */
        ch_read(conn);
    }
}

int
read_child_data(int conn_num)
{
    struct packet *pkt;
    int ret;
    void *conn;
    extern struct packet *ch_alloc_pkt(int size);

    tracef(TRACE_LOW, "read_child_data()");

    /* blocking */
    if (ch_full(child_conn[conn_num].conn))
        return 0;

    pkt = ch_alloc_pkt(512);

    /* 1st byte is cp_op */
    ret = read(child_conn[conn_num].fd_in, pkt->pk_cdata-1, 512);

    pkt->pk_op = /*DATOP*/ pkt->pk_cdata[-1];
    SET_PH_LEN(pkt->pk_phead,ret - 1);

    debugf(DBG_INFO, "read_child: rcv data %d bytes", PH_LEN(pkt->pk_phead));
    if (flag_debug_level > 5) {
        dumpbuffer((u_char *)pkt->pk_cdata, PH_LEN(pkt->pk_phead));
    }
    
    ch_write(child_conn[conn_num].conn, pkt);

    return 0;
}

/*
 * read control connection from child process
 * these are messages from chlib, used to manipulate chaos connections
 * 
 * we send them a "connection #" along with the fd which is used as
 * an index into the child connection table
 *
 */
int
read_child_ctl(void)
{
    struct packet *pkt;
    int ret, len, req, conn_num, mode;
    char ctlbuf[512], contact[64];

    tracef(TRACE_MED, "read_child_ctl()\n");

    /* 1st byte is cp_op */
    ret = read(child_fd_ctl, ctlbuf, 512);

    debugf(DBG_INFO, "read_child: ctl %d bytes\n", ret);
    if (flag_debug_level > 5) {
        dumpbuffer((u_char *)ctlbuf, ret);
    }

    req = ctlbuf[0];
    conn_num = ctlbuf[1];
    mode = ctlbuf[2];

    debugf(DBG_LOW, "read_child: req %d, conn_num %d, mode %o\n",
           req, conn_num, mode);

    switch (req) {
    case 1: /* chstatus */
    	{
            struct chstatus chst;
            struct connection *conn;
            struct packet *pkt;

            memcpy((char *)&chst, &ctlbuf[4], sizeof(struct chstatus));

            conn = child_conn[conn_num].conn;

            chst.st_fhost = CH_ADDR_SHORT(conn->cn_faddr);
            chst.st_cnum = conn->cn_ltidx;
            chst.st_rwsize = SHORT_TO_LE(conn->cn_rwsize);
            chst.st_twsize = SHORT_TO_LE(conn->cn_twsize);
            chst.st_state = conn->cn_state;
            chst.st_cmode = conn->cn_mode;
            chst.st_oroom = SHORT_TO_LE(conn->cn_twsize -
                (conn->cn_tlast - conn->cn_tacked));

            if ((pkt = conn->cn_rhead) != NOPKT) {
                chst.st_ptype = pkt->pk_op;
                chst.st_plength = SHORT_TO_LE(PH_LEN(pkt->pk_phead));
            } else {
                chst.st_ptype = 0;
                chst.st_plength = 0;
            }

            ctlbuf[2] = 0;
            ctlbuf[3] = 0;

            memcpy(&ctlbuf[4], (char *)&chst, sizeof(struct chstatus));
            len = 4 + sizeof(struct chstatus);
            write(child_fd_ctl, ctlbuf, len);
        }
        break;
    case 2: /* chopen */
    	{
            struct chopen rfc;
            int sv[2];
            char cmsgbuf[sizeof(struct cmsghdr) + sizeof(int)];
            struct msghdr msg;
            struct cmsghdr *cmsg;
            struct iovec vector;
            void *conn;
            int err;
            extern void *chopen(struct chopen *co, int mode, int *perr);

            memcpy((char *)&rfc, &ctlbuf[4], sizeof(rfc));
            if (rfc.co_clength > 0) {
                memcpy(contact, &ctlbuf[4 + sizeof(rfc)], rfc.co_clength);
                contact[rfc.co_clength] = 0;
                rfc.co_contact = contact;
            } else
                rfc.co_contact = 0;

            if (rfc.co_length > 0)
                rfc.co_data = &ctlbuf[4 + sizeof(rfc) + rfc.co_clength];
            else
                rfc.co_data = 0;

            debugf(DBG_LOW, "contact '%s' host %o",
                   rfc.co_contact, rfc.co_host);

            /* open socket */
            conn = chopen(&rfc, mode, &err);

            /* make a socket to talk to the connection */
            ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);

            child_conn[child_conn_count].conn = conn;
            child_conn[child_conn_count].fd_out = sv[0];
            child_conn[child_conn_count].fd_in = sv[0];
            child_conn_count++;

            ctlbuf[0] = 2;
            ctlbuf[1] = child_conn_count-1;
            ctlbuf[2] = 0;
            ctlbuf[3] = 0;

            /* send socket fd to the server */
            debugf(DBG_LOW, "sending, sctl %d, fd %d, local %d",
                   child_fd_sctl, sv[1], sv[0]);

            memset(cmsgbuf, 0, sizeof(cmsgbuf));
            cmsg = (struct cmsghdr *)cmsgbuf;
            cmsg->cmsg_len = sizeof(struct cmsghdr) + sizeof(int);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;

            memcpy(CMSG_DATA(cmsg), &sv[1], sizeof(sv[1]));

            /* */
            vector.iov_base = ctlbuf;
            vector.iov_len = 4;

            /* */
            msg.msg_flags = 0;
            msg.msg_control = cmsg;
            msg.msg_controllen = cmsg->cmsg_len;

            msg.msg_name= NULL;
            msg.msg_namelen = 0;
            msg.msg_iov = &vector;
            msg.msg_iovlen = 1;

            ret = sendmsg(child_fd_sctl, &msg, 0);
            if (ret < 0) {
                perror("sendmsg");
            }
        }
        break;

    case 3: /* setmode */
        ret = ch_setmode(child_conn[conn_num].conn, mode);

        ctlbuf[0] = 3;
        ctlbuf[1] = ret;
        ctlbuf[2] = 0;
        ctlbuf[3] = 0;
        write(child_fd_ctl, ctlbuf, 4);
        break;

    }

    return 0;
}

void
start_file(void *conn, char *args, int arglen)
{
    int i;
    char *parg;

    child_conn[0].conn = conn;
    child_conn[0].fd_out = -1;
    child_conn[0].fd_in = -1;
    child_conn_count = 1;

    /* skip over RFC name */
    for (i = 0; i < arglen; i++) {
        if (args[i] == ' ')
            break;
    }
    parg = 0;
    if (args[i] == ' ') {
        args[arglen] = 0;
        parg = &args[i+1];
    }

    fork_file(parg);
}


int
read_chaos(void)
{
    int ret, len;
    unsigned long id;
    unsigned char lenbytes[4];
    u_char *data, *resp;

    tracef(TRACE_LOW, "read_chaos()");

    ret = read(fd, lenbytes, 4);
    if (ret <= 0) {
        return -1;
    }

    len = (lenbytes[0] << 8) | lenbytes[1];

    ret = read(fd, buffer, len);
    debugf(DBG_LOW, "read_chaos() ret=%d\n", ret);

    if (ret <= 0)
        return -1;

    if (ret != len) {
        debugf(DBG_WARN, "read_chaos() length error; len=%d\n", len);
        return -1;
    }

    stats.rx++;
#if 0
    check_packet(buffer, ret);
#else
    ch_rcv_pkt_buffer(buffer, ret);
#endif

    return 0;
}

/*
 * listen on all the various connections
 */
int
chaos_poll(void)
{
#define MAX_UFDS (2+MAX_CHILD_CONN)
    struct pollfd ufds[MAX_UFDS];
    int ufds_type[MAX_UFDS];
    int ufds_conn_num[MAX_UFDS];
    int n = 1;
    int i, ret, timeout;

    for (i = 0; i < MAX_UFDS; i++) {
        ufds[i].revents = 0;
        ufds_type[i] = 0;
        ufds_conn_num[i] = -1;
    }

    /* main connection from chaosd */
    ufds[0].fd = fd;
    ufds[0].events = POLLIN;
    ufds[0].revents = 0;
    ufds_type[0] = 1;

    /* control connection */
    if (child_fd_ctl) {
        ufds[n].fd = child_fd_ctl;
        ufds[n].events = POLLIN;
        ufds_type[n] = 2;
        n++;
    }

    /* all the child connections */
    for (i = 0; i < child_conn_count; i++) {
        ufds[n].fd = child_conn[i].fd_in;
        ufds[n].events = POLLIN;
        ufds_type[n] = 3;
        ufds_conn_num[n] = i;
        n++;
    }

//    timeout = 250;
    timeout = 10;

    ret = poll(ufds, n, timeout);
    if (ret < 0) {
        perror("poll:");
        return -1;
    }

    if (ret == 0) {
    } else {
        for (i = 0; i < n; i++) {

            if (ufds[i].revents == 0)
                continue;

            switch (ufds_type[i]) {
            case 1:
                /* notice when chaosd dies */
                if (ufds[i].revents & POLLHUP) {
                    return -1;
                }

                read_chaos();
                break;
            case 2:
                read_child_ctl();
                break;
            case 3:
                read_child_data(ufds_conn_num[i]);
                break;
            }
        } /* for */
    }

    return 0;
}

int
server_shutdown(void)
{
    log_shutdown();
    server_running = 0;
    return 0;
}

/*
 * fork the server process and set up to be a good background daemon
 *
 * disconnect from terminal, etc...
 */
int
daemonize(char *what)
{
    int r;

    chdir("/tmp");

    if ((r = fork()) > 0)
        exit(0);

    if (r == -1) {
	fprintf(stderr,"%s: unable to fork new process\n", what);
	perror("fork");
	exit(1);
    }

    close(0);
    close(1);
    close(2);

    debugf(DBG_INFO, "chaos server v%d.%d",
           SERVER_VERSION / 100, SERVER_VERSION % 100);
    debugf(DBG_INFO, "%s", rcs_id);

    return 0;
}

void
restart_child(void)
{
}

void
usage(void)
{
    fprintf(stderr, "chaos server v%d.%d\n",
            SERVER_VERSION / 100, SERVER_VERSION % 100);

    fprintf(stderr, "usage:\n");
    fprintf(stderr, "-s        run as a background daemon\n");
    fprintf(stderr, "-d        increment debug level\n");
    fprintf(stderr, "-t        increment trace level\n");
    fprintf(stderr, "-D<level> set debug level\n");
    fprintf(stderr, "-T<level> set trace level\n");

    exit(1);
}

extern char *optarg;

int main(int argc, char *argv[])
{
    int c, waiting;

    while ((c = getopt(argc, argv, "sdD:tT:")) != -1) {
        switch (c) {
        case 's':
            flag_daemon++;
            break;
        case 'd':
	    flag_debug_level++;
            break;
        case 't':
            flag_debug_time++;
            break;
        case 'D':
            flag_debug_level = atoi(optarg);
            break;
        case 'T':
            flag_trace_level = atoi(optarg);
            break;
        default:
            usage();
        }
    }

    if (flag_daemon) {
        daemonize(argv[0]);
    }

    signal_init();
    chaos_init();

    // printf("server: connect_to_server\n");
    if (connect_to_server() == -1) {
        exit(1);
    }

#if 0
    send_testpackets();
#endif

    server_running = 1;

    while (server_running) {
        if (chaos_poll())
            break;

        signal_poll();
    }

    exit(0);
}


/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/

