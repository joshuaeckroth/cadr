/*
 * listen.c
 *
 * basic listening node for chaosd server
 * decodes protocol and prints out packets
 *
 * $Id: listen.c 78 2006-07-18 18:28:34Z brad $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/uio.h>

#include "chaos.h"
#include "chaosd.h"

int verbose;
int show_contents;
int relative_time;

int fd;
struct sockaddr_un unix_addr;
u_char buffer[4096];
u_char *msg, resp[8];

/*
 * connect to server using specificed socket type
 */
int
connect_to_server(void)
{
    int len;

    printf("connect_to_server()\n");

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

    if (verbose > 1) printf("fd %d\n", fd);
        
    return 0;
}

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

void
dump_contents(u_char *buf, int cnt)
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
                printf("  ...\n");
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

            printf("  %08x %s %s\n", offset, line, cbuf);
        }

        buf += 16;
        cnt -= 16;
        offset += 16;
    }

    if (skipping) {
        skipping = 0;
        printf("  %08x ...\n", offset-16);
    }
}

void setcolor_green(void) { printf("\033[1;32m"); }
void setcolor_red(void) { printf("\033[1;31m"); }
void setcolor_yellow(void) { printf("\033[1;33m"); }
void setcolor_normal(void) { printf("\033[0;39m"); }

void
decode_chaos(char *buffer, int len)
{
    struct pkt_header *ph = (struct pkt_header *)buffer;
    time_t t;
    struct tm *tm;
    struct timeval tv;
    int ms;

//    setcolor_green();
    setcolor_red();

    if (!relative_time) {
#if 0
        t = time(NULL);
        ms = 0;
#else
        gettimeofday(&tv, NULL);
        t = tv.tv_sec;
        ms = tv.tv_usec / 1000;
#endif

        tm = localtime(&t);

        printf("%02d:%02d:%02d.%03d ",
               tm->tm_hour, tm->tm_min, tm->tm_sec, ms);
    } else {
        /* relative time */
        static struct timeval tv_last;
        struct timeval tv1, tv2;
        gettimeofday(&tv1, NULL);
        if (tv_last.tv_sec == 0) {
            tv2.tv_sec = tv2.tv_usec = 0;
        } else {
            if (tv_last.tv_usec <= tv1.tv_usec) {
                tv2.tv_usec = tv1.tv_usec - tv_last.tv_usec;
                tv2.tv_sec = tv1.tv_sec - tv_last.tv_sec;
            } else {
                tv2.tv_usec = (tv1.tv_usec + 1 * 1000 * 1000)
                    - tv_last.tv_usec;
                tv2.tv_sec = (tv1.tv_sec - 1) - tv_last.tv_sec;
            }
        }
        printf("%4ld:%06d ", tv2.tv_sec, tv2.tv_usec);
        tv_last = tv1;
    }

    if (ph->ph_op > 0 && ph->ph_op <= BRDOP)
        printf("%s ", popcode_to_text(ph->ph_op));
    else
        printf("%02x ", ph->ph_op);

    printf("to (%o %o:%d,%d) ",
           ph->ph_daddr.subnet, ph->ph_daddr.host,
           ph->ph_didx.tidx, ph->ph_didx.uniq);

    printf("from (%o %o:%d,%d) ",
           ph->ph_saddr.subnet, ph->ph_saddr.host,
           ph->ph_sidx.tidx, ph->ph_sidx.uniq);

    printf("pkn %o ackn %o ",
           LE_TO_SHORT(ph->LE_ph_pkn), 
           LE_TO_SHORT(ph->LE_ph_ackn));

    printf("len %d ", len);
    printf("\n");

    setcolor_normal();

    if (show_contents) dump_contents((u_char *)buffer, len);

    if (0) {
        printf("  opcode %04x %s\n", ph->ph_op, popcode_to_text(ph->ph_op));

        printf("  daddr %o (%o %o), tidx %d, unique %d\n",
               CH_ADDR_SHORT(ph->ph_daddr), 
               ph->ph_daddr.subnet, ph->ph_daddr.host,
               ph->ph_didx.tidx, ph->ph_didx.uniq);

        printf("  saddr %o (%o %o), tidx %d, uniq %d\n",
               CH_ADDR_SHORT(ph->ph_saddr), 
               ph->ph_saddr.subnet, ph->ph_saddr.host,
               ph->ph_sidx.tidx, ph->ph_sidx.uniq);
    }

    fflush(stdout);
}

int
read_chaos(int fd)
{
    int ret, len;
    u_char lenbytes[4];

    ret = read(fd, lenbytes, 4);
    if (ret <= 0) {
        return -1;
    }

    len = (lenbytes[0] << 8) | lenbytes[1];

    ret = read(fd, buffer, len);
    if (ret <= 0)
        return -1;

    decode_chaos((char *)buffer, len);

    return 0;
}

void
usage(void)
{
    fprintf(stderr, "listen - display chaosnet traffic\n");
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "-a         show absolute time\n");
    fprintf(stderr, "-v         verbose mode\n");
    fprintf(stderr, "-s         show packet contents\n");
    exit(1);
}

extern char *optarg;

int
main(int argc, char *argv[])
{
    int c, waiting;

    relative_time = 1;

    while ((c = getopt(argc, argv, "asv")) != -1) {
        switch (c) {
        case 'a':
            relative_time = 0;
            break;
        case 's':
            show_contents++;
            break;
        case 'v':
            verbose++;
            break;
        default:
            usage();
        }
    }

    if (connect_to_server()) {
        exit(1);
    }

    while (1) {
        if (read_chaos(fd))
            break;
    }

    exit(0);
}


/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/
