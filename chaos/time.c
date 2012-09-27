/*
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/uio.h>

#define UNIX_SOCKET_PATH	"/var/tmp/"
#define UNIX_SOCKET_CLIENT_NAME	"chaosd_"
#define UNIX_SOCKET_SERVER_NAME	"chaosd_server"
#define UNIX_SOCKET_PERM	S_IRWXU

int verbose;
int fd;
struct sockaddr_un unix_addr;
u_char buffer[4096];
u_char *msg, resp[8];

void node_demux(unsigned long id);

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
//    len = strlen(unix_addr.sun_path) + sizeof(unix_addr.sun_family);
    len = strlen(unix_addr.sun_path) + sizeof unix_addr - sizeof unix_addr.sun_path;

    unlink(unix_addr.sun_path);

    if ((bind(fd, (struct sockaddr *)&unix_addr, len) < 0)) {
      perror("bind(AF_UNIX)");
      return -1;
    }

    if (chmod(unix_addr.sun_path, UNIX_SOCKET_PERM) < 0) {
      perror("chmod(AF_UNIX)");
      return -1;
    }

//    sleep(1);
        
    memset(&unix_addr, 0, sizeof(unix_addr));
    sprintf(unix_addr.sun_path, "%s%s",
	    UNIX_SOCKET_PATH, UNIX_SOCKET_SERVER_NAME);
    unix_addr.sun_family = AF_UNIX;
//    len = strlen(unix_addr.sun_path) + sizeof(unix_addr.sun_family);
    len = strlen(unix_addr.sun_path) + sizeof unix_addr - sizeof unix_addr.sun_path;

    if (connect(fd, (struct sockaddr *)&unix_addr, len) < 0) {
      perror("connect(AF_UNIX)");
      return -1;
    }

    if (verbose > 1) printf("fd %d\n", fd);
        
    return 0;
}

void
send_chaos(void)
{
    u_short b[64];
    u_char lenbytes[4];
    struct iovec iov[2];
    int wsize, plen, ret;

    wsize = 0;
    memset((char *)b, 0, sizeof(b));

    printf("sending fake time packet\n");
    b[wsize++] = htons(5); /* op = ANS */
    b[wsize++] = htons(16+8); /* count*/
    b[wsize++] = 0;
    b[wsize++] = 0;
    b[wsize++] = 0;
    b[wsize++] = 0;
    b[wsize++] = 0;
    b[wsize++] = 0;
    b[wsize++] = htons(('T' << 8) | 'I');
    b[wsize++] = htons(('M' << 8) | 'E');
    b[wsize++] = 0;
    b[wsize++] = 0;

    b[wsize++] = htons(0401); /* dest */
    b[wsize++] = 0; /* source */
    b[wsize++] = 0; /* checksum */


    plen = wsize*2;

    lenbytes[0] = plen >> 8;
    lenbytes[1] = plen;
    lenbytes[2] = 1;
    lenbytes[3] = 0;

    iov[0].iov_base = lenbytes;
    iov[0].iov_len = 4;

    iov[1].iov_base = b;
    iov[1].iov_len = wsize*2;

    ret = writev(fd, iov, 2);
    if (ret <  0) {
        perror("writev");
    }
}

main()
{
    int waiting;

    if (connect_to_server()) {
        exit(1);
    }

    while (1) {
        sleep(1);
        send_chaos();
    }

    exit(0);
}


/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/
