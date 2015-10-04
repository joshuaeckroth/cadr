/*
 * node.c
 *
 * keep track of client nodes which have connected
 *
 * $Id: node.c 69 2006-07-11 16:49:59Z brad $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "chaosd.h"
#include "log.h"
#include "FILE.h"

#define MAX_NODES	10

int node_count;
node_t *nodes[MAX_NODES];

extern int flag_debug_level;

int
node_new(node_t **pnode)
{
    node_t *node;

    if (node_count >= MAX_NODES)
        return -1;

    node = (node_t *)malloc(sizeof(node_t));
    if (node == 0)
        return -1;

    memset((char *)node, 0, sizeof(node_t));

    debugf(DBG_LOW, "node_new() [%d] = %p\n", node_count, node);

    node->index = node_count;
    nodes[node_count++] = node;

#if 1
    if (flag_debug_level > 5)
    {
        int i;
        for (i = 0; i < node_count; i++) {
            debugf(DBG_LOW, "nodes[%d] %p, fd %d\n",
                   i, (void *)nodes[i], nodes[i] ? nodes[i]->fd : -1);
        }
    }
#endif

    *pnode = (void *)node;
    return 0;
}

int
node_destroy(node_t *node)
{
    int i;

    i = node->index;
    debugf(DBG_LOW, "node_destroy(%p) removing index %d\n", node, i);

    nodes[i] = 0;

    /* if it's not the last one, pull up the vector */
    for (; i < node_count-1; i++) {
        nodes[i] = nodes[i+1];
        nodes[i]->index = i;
    }

    free((char *)node);

    node_count--;

#if 1
    if (flag_debug_level > 5) {
        for (i = 0; i < node_count; i++) {
            debugf(DBG_LOW, "[%d] %p, fd %d\n",
                   i, (void *)nodes[i], nodes[i] ? nodes[i]->fd : -1);
        }
    }
#endif

    return 0;
}

/*
 * callback routine called when an socket is closed down;
 * resets the state of the client object and erases the client
 */
int
node_close(int fd, void *node, int context)
{
    int ret, xfd;
    void *pnode;

    debugf(DBG_LOW, "node_close(fd=%d,node=%p)\n", fd, node);
    
    /* don't need to close the fd - transport does this */
    
    if (fd_context_valid(context, &xfd, &pnode))
        return -1;

    node_destroy((node_t *)node);
    
    return 0;
}

void
node_set_fd(node_t *node, int fd)
{
    node->fd = fd;
}

/* read a message serialized on a stream */
int
node_stream_reader(int fd, void *void_node, int context)
{
    int i, ret, size, op;
    unsigned long len, id;
    u_char lenbytes[4], msg[4096];
    struct iovec iov[2];


    debugf(DBG_LOW, "node_stream_reader(fd=%d)\n", fd);

    ret = read(fd, lenbytes, 4);
    if (ret <= 0) {
        debugf(DBG_INFO | DBG_ERRNO, "read header error, ret %d\n", ret);
        return -1;
    }

    if (ret != 4) {
        debugf(DBG_INFO | DBG_ERRNO,
               "length data error, ret %d != 4: %04X %04X %04X %04X\n",
               ret, lenbytes[0], lenbytes[1], lenbytes[2], lenbytes[3]);
        return -1;
    }

    len = (lenbytes[0] << 8) | lenbytes[1];

    ret = read(fd, msg, len);
    if (ret <= 0) {
        debugf(DBG_INFO | DBG_ERRNO, "read data error, ret %d\n", ret);
        return -1;
    }

    if (ret != len) {
        debugf(DBG_INFO | DBG_ERRNO,
               "length data error, ret %d != len %d: "
               "%04X %04X %04X %04X\n",
               ret, len,
               lenbytes[0], lenbytes[1], lenbytes[2], lenbytes[3]);
        return -1;
    }

    size = ret;

    debugf(DBG_LOW, "node_stream_reader(fd=%d) node_count %d\n",
           fd, node_count);

    op = msg[0] | (msg[1] << 8);

    debugf(DBG_LOW,
           "op %04x data %02x%02x%02x%02x%02x%02x%02x%02x\n",
           op, 
           msg[16], msg[17], msg[18], msg[19],
           msg[20], msg[21], msg[22], msg[23]);

    if (flag_debug_level > 2) {
        printf("\n");
        dumpbuffer(msg, size);
    }

    lenbytes[2] = 1;
    lenbytes[3] = 0;

    iov[0].iov_base = lenbytes;
    iov[0].iov_len = 4;

    iov[1].iov_base = msg;
    iov[1].iov_len = size;

    for (i = 0; i < node_count; i++) {
        debugf(DBG_LOW, "[%d] %x %x, fd %d\n",
               i, void_node, (void *)nodes[i], nodes[i] ? nodes[i]->fd : -1);

#if 0
        if (void_node == (void *)nodes[i])
            continue;
#endif

        ret = writev(nodes[i]->fd, iov, 2);

        if (ret < 0) {
            debugf(DBG_WARN, "writev() failed, fd %d, ret=%d, size=%d\n", nodes[i]->fd, ret, size);
            perror("writev");
        }

        debugf(DBG_LOW, "send to fd %d, ret=%d\n", nodes[i]->fd, ret);
    }

    return 0;
}


int
node_init(void)
{
    return 0;
}

int
node_poll(void)
{
    return 0;
}


/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/
