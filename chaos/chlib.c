/*
 * chlib.c
 *
 * chaos support library for unix chaos servers
 * communicated with chaos server to manipulate chaos connections
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/un.h>
//#include <stropts.h>

#include "chaos.h"

int map_fd_to_conn[256];

#ifdef linux
char *crypt(char *s1, char *s2)
{
	return 0;
}
#endif

char *
chaos_name(short addr)
{
	printf("chaos_name(addr=%o)\n", addr);
	return "server";
}

int
chstatus(int fd, struct chstatus *chst)
{
	char buffer[512];
	int len, ret, conn_num;

	fprintf(stderr, "chstatus(fd=%d, chst=%p)\n", fd, chst); fflush(stderr);

	conn_num = map_fd_to_conn[fd];

	buffer[0] = 1;
	buffer[1] = conn_num;
	buffer[2] = 0;
	buffer[3] = 0;

	memcpy(&buffer[4], (char *)chst, sizeof(struct chstatus));
	len = 4 + sizeof(struct chstatus);

	ret = write(3, buffer, len);
	ret = read(3, buffer, len);
	fprintf(stderr, "chstatus: read ret %d\n", ret); fflush(stderr);

	memcpy((char *)chst, &buffer[4], sizeof(struct chstatus));

	fprintf(stderr, "cnum %o, fhost %o, state %d\n",
	       chst->st_cnum, chst->st_fhost, chst->st_state); fflush(stderr);

	return 0;
}

int
chopen(int address, char *contact, int mode, int async, char *data, int dlength, int rwsize)
{
	struct chopen rfc;
	int f, connfd = -1;
	char buffer[512];
	int ret, len;
	char cmsgbuf[sizeof(struct cmsghdr) + sizeof(int)];
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec vector;

	fprintf(stderr, "chopen(address=%o,contact=%s)\n", address, contact); fflush(stderr);

	rfc.co_host = address;
	rfc.co_contact = contact;
	rfc.co_data = data;
	rfc.co_length = data ? (dlength ? dlength : strlen(data)) : 0;
	rfc.co_clength = strlen(contact);
	rfc.co_async = async;
	rfc.co_rwsize = rwsize;

	len = 4 + sizeof(rfc) + rfc.co_clength + rfc.co_length;

	fprintf(stderr, "chopen: clength %d, length %d\n", rfc.co_clength, rfc.co_length); fflush(stderr);

	/* send chopen request */
	buffer[0] = 2;
	buffer[1] = 0;
	buffer[2] = mode;
	buffer[3] = async;
	memcpy(&buffer[4], (char *)&rfc, sizeof(rfc));
	memcpy(&buffer[4 + sizeof(rfc)], rfc.co_contact, rfc.co_clength);

	if (rfc.co_data && rfc.co_length > 0)
		memcpy(&buffer[4 + sizeof(rfc)+rfc.co_length], rfc.co_data, rfc.co_length);

	fprintf(stderr, "chopen: write\n"); fflush(stderr);
	write(3, buffer, len);

	/* read back status and connection fd */
	memset(cmsgbuf, 0, sizeof(cmsgbuf));
	cmsg = (struct cmsghdr *)cmsgbuf;
	cmsg->cmsg_len = sizeof(struct cmsghdr) + sizeof(int);

	vector.iov_base = buffer;
	vector.iov_len = 4;

	/* */
	msg.msg_flags = 0;
	msg.msg_control = cmsg;
	msg.msg_controllen = cmsg->cmsg_len;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &vector;
	msg.msg_iovlen = 1;

	ret = recvmsg(4, &msg, 0);
	fprintf(stderr, "chopen: recvmsg ret %d\n", ret); fflush(stderr);
	if (ret < 0) {
                perror("recvmsg");
		fflush(stderr);
	}

	memcpy(&connfd, CMSG_DATA(cmsg), sizeof(connfd));

	fprintf(stderr, "chopen: connfd %d, conn_num %d\n", connfd, buffer[1]); fflush(stderr);

	if (connfd > 0) {
		map_fd_to_conn[connfd] = buffer[1];
	}

	return connfd;
}


int 
chwaitfornotstate(int fd, int state)
{
	int ret;

	fprintf(stderr, "chwaitfornotstate(fd=%d, state=%d)\n", fd, state); fflush(stderr);

	while (1) {
		struct chstatus chst;

		fprintf(stderr, "chwaitfornotstate(fd=%d, state=%d) loop\n", fd, state);
		fflush(stderr);

		ret = chstatus(fd, &chst);
		if (ret < 0)
			return ret;

		if (chst.st_state != state)
			break;

		sleep(1);
	}

	fprintf(stderr, "chwaitfornotstate(fd=%d, state=%d) done\n", fd, state); fflush(stderr);

	return 0;
}

int
chsetmode(int fd, int mode)
{
	char buffer[512];
	int len, ret, conn_num;

	fprintf(stderr, "chsetmode(fd=%d, mode=%d)\n", fd, mode); fflush(stderr);

	conn_num = map_fd_to_conn[fd];

	buffer[0] = 3;
	buffer[1] = conn_num;
	buffer[2] = mode;
	buffer[3] = 0;
	len = 4;

	ret = write(3, buffer, len);
	ret = read(3, buffer, len);
	fprintf(stderr, "chsetmode: read ret %d\n", ret); fflush(stderr);

	return 0;
}

int
chlisten(char *contact, int mode, int async, int rwsize)
{
	printf("chlisten(contact=%s)\n", contact);

	return chopen(0, contact, mode, async, 0, 0, rwsize);
}

void
chreject(int fd, char *string)
{
	struct chreject chr;

	printf("chreject(fd=%d, string=%s)\n", fd, string);

	if(string==0||strlen(string)==0)
	  string = "No Reason Given";
	chr.cr_reason = string;
	chr.cr_length = strlen(string);
//	return ioctl(fd, CHIOCREJECT, &chr);
}

