/*
 * chaos_socket.c
 *
 * Common socket routines for user-space CHAOS network simulation
 * November 2005
 * JAO = JosephOswald@gmail.com
 * based on common code from all of the CHAOS client applications.
 *
 * $Version$
 *
 * $Log$
 *
 */

/* 
 * Essentially, there are two sockets involved
 * /var/tmp/chaosd/chaosd_server for the central server that echoes packets
 * to all the clients, and
 * /var/tmp/chaosd/chaosd_<PID> for each client
 *
 * Previously, this code was duplicated in each client, and seemed to
 * have a subtle problem that caused it not to work under Mac OS X. 
 * Instead of changing each copy, I (JAO) brought it into this common
 * file.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "chaosd.h"

#ifndef S_IRWXU /* JAO */
#define	S_IRWXU		0000700		/* [XSI] RWX mask for owner */
#endif

struct sockaddr_un unix_addr;

/* make_server_address: bash a struct sockaddr to contain
   the UNIX address of the CHAOS user-mode server. 
   Returns the address length in bytes, or -1 if the process fails.
*/
int make_server_address(struct sockaddr_un *unix_addr_ptr) {
  
  int name_len, total_len, result;

  if (!unix_addr_ptr) { return -1; }
  
  memset(unix_addr_ptr, 0, sizeof(unix_addr));

  unix_addr_ptr->sun_family = AF_UNIX;
  name_len = sprintf(unix_addr_ptr->sun_path, "%s%s",
		     UNIX_SOCKET_PATH, UNIX_SOCKET_SERVER_NAME);

#if(0)
  result = unlink(unix_addr_ptr->sun_path); 
  if (result && errno != ENOENT) {
    perror("make_server_address (unlink):");
    fprintf(stderr, "unlink: %s\n", unix_addr_ptr->sun_path);
  }
#endif

  /* name_len does not include the final NULL */
  /* NB. Mac OS X includes an extra sun_len field in the 
     sockaddr structure. */

  return SUN_LEN(unix_addr_ptr);  
}

/* make_client_address: bash a struct sockaddr to contain
   the UNIX address of the CHAOS user-mode client. 
   Returns the address length in bytes, or -1 if the process fails.
*/
int make_client_address(struct sockaddr_un *unix_addr_ptr) {
  
  int name_len, total_len;
  int result;

  if (!unix_addr_ptr) { return -1; }
  
  memset(unix_addr_ptr, 0, sizeof(unix_addr));

  unix_addr_ptr->sun_family = AF_UNIX;
  name_len = sprintf(unix_addr_ptr->sun_path, "%s%s%05u",
		     UNIX_SOCKET_PATH, UNIX_SOCKET_CLIENT_NAME,
		     getpid());

#if(0)
  result = unlink(unix_addr_ptr->sun_path);
  if (result && errno != ENOENT) {
    perror("make_client_address (unlink):");
    fprintf(stderr, "unlink: %s\n", unix_addr_ptr->sun_path);
  }
#endif

  /* name_len does not include the final NULL */
  /* NB. Mac OS X includes an extra sun_len field in the 
     sockaddr structure. */

  return SUN_LEN(unix_addr_ptr);  
}

/* connect_to_server: makes a client connection to the user-mode
   CHAOS server, returning a file descriptor, or -1 if the 
   connection fails. */

int connect_to_server(void) {

    int len, fd;

    printf("connect_to_server()\n");

    if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
      perror("connect_to_server: socket(AF_UNIX)");
      return -1;
    }

    len = make_client_address(&unix_addr);
	unlink(unix_addr.sun_path);

    if ((bind(fd, (struct sockaddr *)&unix_addr, len) < 0)) {
      perror("bind(AF_UNIX)");
      return -1;
    }

#if(1) /* JAO: gives problems on Mac OS X...false alarm due to earlier name mismatches */
    if (chmod(unix_addr.sun_path, UNIX_SOCKET_PERM) < 0) {
      perror("chmod(AF_UNIX)");
      return -1;
    }
#endif

//    sleep(1);
        
    len = make_server_address(&unix_addr);

    if (connect(fd, (struct sockaddr *)&unix_addr, len) < 0) {
      printf("connect: %s\n",unix_addr.sun_path);
      perror("connect(AF_UNIX)");
      return -1;
    }

    return fd;
}
