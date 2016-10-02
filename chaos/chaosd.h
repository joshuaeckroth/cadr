/*
 * $Id: chaosd.h 48 2005-10-04 19:48:14Z brad $
 */

#ifndef CHAOSD_H
#define CHAOSD_H

#define CHAOSD_SERVER_VERSION 003

#define UNIX_SOCKET_PATH	"/var/tmp/chaosd/"
#define UNIX_SOCKET_CLIENT_NAME	"chaosd_"
#define UNIX_SOCKET_SERVER_NAME	"chaosd_server"
#define UNIX_SOCKET_PERM	S_IRWXU

typedef struct node_s {
    int fd;
    int index;
} node_t;

int node_init(void);
int node_poll(void);
int node_new(node_t **pnode);
int node_close(int fd, void *node, int context);
int node_stream_reader(int fd, void *void_client, int context);
void node_set_fd(node_t *node, int fd);

int fd_add_listen(int fd, int (*accept_func)(int fd));
int fd_add_reader(int fd,
                  int (*read_func)(int fd, void *server, int context),
                  int (*close_func)(int fd, void *server, int context),
                  void *server);
int fd_poll(void);
int fd_context_valid(int context, int *pfd, void **pserver);


int signal_init(void);
void signal_poll(void);

#endif


/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/
