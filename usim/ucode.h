/*
 * ucode.h
 * $Id: ucode.h 66 2006-07-11 15:48:06Z brad $
 */

#if defined(LINUX) || defined(OSX)
typedef long long int64;
typedef unsigned long long uint64;

#define NOP_MASK 03777777777767777LL
#define O_BINARY 0
#endif /* LINUX */


#ifdef WIN32
typedef __int64 int64;
typedef __int64 uint64;

struct timeval {
	unsigned int tv_sec;
	unsigned int tv_usec;
};

typedef unsigned char u_char;
typedef unsigned long off_t;

#define inline 
#define NOP_MASK 03777777777767777
#endif /* WIN32 */

typedef uint64 ucw_t;

extern int trace;
#define tracef	if (trace) printf

extern int trace_io_flag;
#define traceio	if (trace_io_flag) printf

extern int trace_disk_flag;
#define tracedio if (trace_disk_flag) printf

extern int trace_net_flag;
#define tracenet if (trace_net_flag) printf

extern int trace_int_flag;
#define traceint if (trace_int_flag) printf

extern int trace_vm_flag;
#define tracevm if (trace_vm_flag) printf

extern int run_ucode_flag;
extern int warm_boot_flag;
extern int trace_mcr_labels_flag;
extern int trace_lod_labels_flag;
extern int trace_prom_flag;
extern int trace_mcr_flag;
extern int stop_after_prom_flag;
extern int alt_prom_flag;

extern unsigned long cycles;
extern unsigned long max_cycles;
extern unsigned long max_trace_cycles;

extern char *sym_find_by_val(int mcr, int v);
extern char *sym_find_last(int mcr, int v, int *poffset);
extern char *sym_find_by_type_val(int mcr, int t, int v);

extern int breakpoint_set_mcr(char *arg);
extern int breakpoint_set_prom(char *arg);
extern int breakpoint_set_count(int count);

extern int tracelabel_set_mcr(char *arg);

extern int read_phy_mem(int paddr, unsigned int *pv);
extern int write_phy_mem(int paddr, unsigned int v);

