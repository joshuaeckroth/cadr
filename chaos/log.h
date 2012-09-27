/*
 * log.h
 *
 * $Id: log.h 56 2005-12-13 20:13:39Z brad $
 */

enum {
	DBG_WARN = 1,
	DBG_INFO,
	DBG_LOW
};

#define DBG_ERRNO	0x80

enum {
	TRACE_HIGH = 1,
	TRACE_MED,
	TRACE_LOW
};

void log(int level, char *fmt, ...);


/*
 * Local Variables:
 * indent-tabs-mode:nil
 * c-basic-offset:4
 * End:
*/

