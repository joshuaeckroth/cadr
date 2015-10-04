
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

int log_init(void);
int log_shutdown(void);
void write_log(int level, char *fmt, ...);
void debugf(int level, char *fmt, ...);
void tracef(int level, char *fmt, ...);

