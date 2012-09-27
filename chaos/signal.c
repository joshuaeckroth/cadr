/*
 * signal.c
 *
 * unix signal code
 *
 * Brad Parker <brad@@heeltoe.com>
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>

#include "log.h"

int got_sighup;
int got_sigterm;
int got_sigchld;

void sighup_handler(int sig)
{
    got_sighup++;
}

void sigterm_handler(int sig)
{
    got_sigterm++;
}

void sigpipe_handler(int sig)
{
    printf("sigpipe!\n");
}

void sigchld_handler(int sig)
{
    printf("sigchld!\n");
    got_sigchld++;
}

int
signal_init(void)
{
    struct sigaction new, old;

    memset((char *)&new, 0, sizeof(new));
    new.sa_handler = sighup_handler;
    sigaction(SIGHUP, &new, &old);

    memset((char *)&new, 0, sizeof(new));
    new.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &new, &old);

    memset((char *)&new, 0, sizeof(new));
    new.sa_handler = sigpipe_handler;
    sigaction(SIGPIPE, &new, &old);

    memset((char *)&new, 0, sizeof(new));
    new.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &new, &old);

    return 0;
}

void
signal_poll(void)
{
    if (got_sighup) {
        debugf(DBG_WARN, "got signal SIGHUP; ignoring\n");
    }

    if (got_sigterm) {
        debugf(DBG_WARN, "got signal SIGTERM; shutting down\n");
        server_shutdown();
    }

    if (got_sigchld) {
        debugf(DBG_WARN, "got signal SIGCHLD; child died\n");
        restart_child();
    }
}



/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/

