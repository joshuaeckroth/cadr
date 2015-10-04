/*
 * log.c
 *
 * support for syslog and debug logging
 *
 * Brad Parker <brad@heeltoe.com>
 * $Id: log.c 58 2005-12-14 00:08:53Z brad $
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <syslog.h>
#include <time.h> 

#include "log.h"

#define LOG_IDENT "chaosd server"

extern int flag_daemon;
extern int flag_debug_level;
extern int flag_trace_level;
extern int flag_debug_time;

int
log_init(void)
{
    if (flag_daemon) {
        openlog(LOG_IDENT, LOG_CONS, LOG_DAEMON);
    }
    return 0;
}

void
write_log(int level, char *fmt, ...)
{
    char string[512];
    va_list ap;
    
    va_start(ap, fmt);
    vsprintf(string, fmt, ap);
    va_end(ap);

    if (flag_daemon) {
        syslog(level, "%s", string);
    } else {
        printf("log: %s\n", string);
    }
}

int
log_shutdown(void)
{
    if (flag_daemon) {
        closelog();
    }
    return 0;
}

void
debugf(int level, char *fmt, ...)
{
    char string[512], intro[64], *tail, *head;
    va_list ap;
    int len;
    int perror_no = 0;
    const char *perror;

    if ((level & 0xf) > flag_debug_level)
        return;

    /* if system error, note number and string */
    if (level & DBG_ERRNO) {
        perror_no = errno;
        perror = strerror(errno);
        if (errno != 0)
            perror = "";
    }

    va_start(ap, fmt);
    vsprintf(string, fmt, ap);
    va_end(ap);

    len = strlen(string);
    tail = string[len-1] == '\n' ? "" : "\n";

    switch (level & 0xf) {
    case DBG_LOW:  head = "    "; break;
    case DBG_INFO: head = "  "; break;
    case DBG_WARN: head = ""; break;
    }

    strcpy(intro, "debug:");

    if (flag_debug_time) {
        time_t t;
        struct tm *tm;
        t = time(NULL);
        tm = localtime(&t);
        sprintf(intro, "%02d:%02d:%02d debug:",
                tm->tm_hour, tm->tm_min, tm->tm_sec);
    }

    if (!flag_daemon) {
        printf("%s %s%s%s", intro, head, string, tail);
        if (perror_no) {
            printf("%s errno %d: %s\n", intro, perror_no, perror);
        }
    }

    if (flag_daemon) {
        if (level >= DBG_WARN) {
            syslog(LOG_WARNING, "debug: %s", string);
            if (perror_no) {
                syslog(LOG_WARNING, "debug: errno %d: %s\n",
                       perror_no, perror);
            }
        }
    }
}

void
tracef(int level, char *fmt, ...)
{
    char string[512], intro[64], *tail, *head;
    va_list ap;
    int len;
    int perror_no = 0;
    const char *perror;

    if ((level & 0xf) > flag_trace_level)
        return;

    /* if system error, note number and string */
    if (level & DBG_ERRNO) {
        perror_no = errno;
        perror = strerror(errno);
        if (errno != 0)
            perror = "";
    }

    va_start(ap, fmt);
    vsprintf(string, fmt, ap);
    va_end(ap);

    len = strlen(string);
    tail = string[len-1] == '\n' ? "" : "\n";

    switch (level & 0xf) {
    case DBG_LOW:  head = "    "; break;
    case DBG_INFO: head = "  "; break;
    case DBG_WARN: head = ""; break;
    }

    strcpy(intro, "trace:");

    if (!flag_daemon) {
        printf("%s %s%s%s", intro, head, string, tail);
        if (perror_no) {
            printf("%s errno %d: %s\n", intro, perror_no, perror);
        }
    }

    if (flag_daemon) {
        if (level >= DBG_WARN) {
            syslog(LOG_WARNING, "trace: %s", string);
            if (perror_no) {
                syslog(LOG_WARNING, "trace: errno %d: %s\n",
                       perror_no, perror);
            }
        }
    }
}


/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/
