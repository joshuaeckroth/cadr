#ifndef FILE_H
#define FILE_H

#include "chaos.h"
#include <pwd.h>
#include <sys/stat.h>

#define BSIZE 512
#define SBLOCK 8
#define FSBSIZE BSIZE

/*
 * Protocol errors
 */
struct file_error	{
	char		*e_code;	/* Standard three letter code */
	char		*e_string;	/* Standard error message string */
};

/*
 * Types of error as reported in error().
 */
#define E_COMMAND	'C'		/* Command error */
#define	E_FATAL		'F'		/* Fatal transfer error */
#define	E_RECOVERABLE	'R'		/* Recoverable transfer error */

#ifdef DEFERROR
#define ERRDEF(x,y) {x,y},
#else
#define ERRDEF(x,y);
#endif
/*
 * Definitions of errors
 * If errstring is not set before calling error(), the string below is used.
 * This macro stuff is gross but necessary.
 */
struct file_error errors[48]
#ifdef DEFERROR
 = {
#endif
/*	code	message */
ERRDEF("000",	"No error")
#define ATD	1
ERRDEF("ATD",	"Incorrect access to directory")
#define ATF	2
ERRDEF("ATF",	"Incorrect access to file")
#define DND	3
ERRDEF("DND",	"Dont delete flag set")
#define IOD	4
ERRDEF("IOD",	"Invalid operation for directory")
#define IOL	5
ERRDEF("IOL",	"Invalid operation for link")
#define IBS	6
ERRDEF("IBS",	"Invalid byte size")
#define IWC	7
ERRDEF("IWC",	"Invalid wildcard")
#define RAD	8
ERRDEF("RAD",	"Rename across directories")
#define REF	9
ERRDEF("REF",	"Rename to existing file")
#define WNA	10
ERRDEF("WNA",	"Wildcard not allowed")
#define ACC	11
ERRDEF("ACC",	"Access error")
#define BUG	12
ERRDEF("BUG",	"File system bug")
#define CCD	13
ERRDEF("CCD",	"Cannot create directory")
#define CCL	14
ERRDEF("CCL",	"Cannot create link")
#define CDF	15
ERRDEF("CDF",	"Cannot delete file")
#define CIR	16
ERRDEF("CIR",	"Circular link")
#define CRF	17
ERRDEF("CRF",	"Rename failure")
#define CSP	18
ERRDEF("CSP",	"Change property failure")
#define DAE	19
ERRDEF("DAE",	"Directory already exists")
#define DAT	20
ERRDEF("DAT",	"Data error")
#define DEV	21
ERRDEF("DEV",	"Device not found")
#define DNE	22
ERRDEF("DNE",	"Directory not empty")
#define DNF	23
ERRDEF("DNF",	"Directory not found")
#define FAE	24
ERRDEF("FAE",	"File already exists")
#define FNF	25
ERRDEF("FNF",	"File not found")
#define FOO	26
ERRDEF("FOO",	"File open for output")
#define FOR	27
ERRDEF("FOR",	"Filepos out of range")
#define HNA	28
ERRDEF("HNA",	"Host not available")
#define ICO	29
ERRDEF("ICO",	"Inconsistent options")
#define IP	30
ERRDEF("IP?",	"Invalid password")
#define IPS	31
ERRDEF("IPS",	"Invalid pathname syntax")
#define IPV	32
ERRDEF("IPV",	"Invalid property value")
#define LCK	33
ERRDEF("LCK",	"File locked")
#define LNF	34
ERRDEF("LNF",	"Link target not found")
#define LIP	35
ERRDEF("LIP",	"Login problems")
#define MSC	36
ERRDEF("MSC",	"Misc problems")
#define NAV	37
ERRDEF("NAV",	"Not available")
#define NER	38
ERRDEF("NER",	"Not enough resources")
#define NET	39
ERRDEF("NET",	"Network lossage")
#define NFS	40
ERRDEF("NFS",	"No file system")
#define NLI	41
ERRDEF("NLI",	"Not logged in")
#define NMR	42
ERRDEF("NMR",	"No more room")
#define UKC	43
ERRDEF("UKC",	"Unknown operation")
#define UKP	44
ERRDEF("UKP",	"Unknown property")
#define UNK	45
ERRDEF("UNK",	"Unknown user")
#define UUO	46
ERRDEF("UUO",	"Unimplemented option")
#define WKF	47
ERRDEF("WKF",	"Wrong kind of file")
#ifdef DEFERROR
}
#endif
;

/*
 * Fatal internal error messages
 */
#define NOMEM		"Out of memory"
#define BADSYNTAX	"Bad syntax definition in program"
#define CTLWRITE	"Write error on control connection"
#define BADFHLIST	"Bad file_handle list"
#define FSTAT		"Fstat failed"

/*
 * Our own error messages for error responses,
 */
#define WRITEDIR	"Access denied for modifying directory."
#define SEARCHDIR	"Access denied for searching directory."
#define READDIR		"Access denied for reading directory."
#define WRITEFILE	"Access denied for writing file."
#define READFILE	"Access denied for reading file."
#define PERMFILE	"Access denied on file."
#define PATHNOTDIR	"One of the pathname components is not a directory."
#define MISSDIR		"Directory doesn't exist."
#define MISSBRACK	"Missing ] in wild card syntax"

extern char errtype;			/* Error type if not E_COMMAND */
extern char *errstring;			/* Error message if non-standard */
#define ERRSIZE 100
extern char errbuf[ERRSIZE + 1];	/* Buffer for building error messages */
extern int globerr;			/* Error return from glob() */
/*extern char *sys_errlist[]; */		/* System error messages */

#if defined(__APPLE__) && defined(__MACH__)
#define OSX
#endif

/*
 * Values for x_state.
 */
#define	X_PROCESS	0	/* Work is in process - everything is ok */
#define X_DONE		1	/* Successful completion has occurred */
				/* Waiting to close normally */
/*
 * States for the input side only.
 */
#define X_BROKEN	2	/* Data connection disappeared on READ */
#define X_ABORT		3	/* Fatal error in reading. Awaiting CLOSE */
#define X_REOF		4	/* Read an EOF and sent all the data */
#define X_SEOF		5	/* Sent an EOF, awaiting FILEPOS or CLOSE */
#define X_IDLE		6	/* Waiting to start */
#define X_DERROR	7	/* Directory list error */
/*
 * States for the output side only.
 */
#define	X_RSYNC		8	/* Received SYNCMARK, waiting for CLOSE */
#define	X_WSYNC		9	/* Transfer punted or EOF, waiting for
				   SYNCMARK */
#define X_ERROR		10	/* Hung after recoverable error */
				/* Waiting to continue or close abnormally */
#define X_RETRY		11	/* After a continue, waiting to retry the
				   recoverable operation. */
/*
 * Values for x_flags
 */
#define X_EOF		BIT(1)	/* EOF has been read from network */
#define X_QUOTED	BIT(2)	/* Used in character set translation */
#define	X_CLOSE		BIT(3)	/* CLOSE command received */
#define X_DELETE	BIT(4)	/* File deleted (close is an abort) */
#define X_ATIME		BIT(5)	/* Change access time at close */
#define X_MTIME		BIT(6)	/* Change mod time at close */

#define	TNULL	((struct transaction *) 0)

#define	CNULL	((struct command *)0)
/*
 * Bit values for c_flags
 */
#define	C_FHMUST	BIT(0)	/* File handle must be present */
#define	C_FHCANT	BIT(1)	/* File handle can't be present */
#define C_FHINPUT	BIT(2)	/* File handle must be for input */
#define C_FHOUTPUT	BIT(3)	/* File handle must be for output */
#define C_XFER		BIT(4)	/* Command should be queued on a transfer */
#define C_NOLOG		BIT(5)	/* Command permitted when not logged in */

#define PNULL	((struct plist *)0)

#define	FNULL	((struct file_handle *)0)

/*
 * Values for f_type
 */
#define	F_INPUT		0	/* Input side of connection */
#define F_OUTPUT	1	/* Output size of connection */

/*
 * Structure describing each transfer in progress.
 * Essentially each OPEN or DIRECTORY command creates a transfer
 * task, which runs (virtually) asynchronously with respect to the
 * control/command main task.  Certain transactions/commands
 * are performed synchronously with the control/main process, like
 * OPEN-PROBES, completion, etc., while others relate to the action
 * of a transfer process (e.g. SET-BYTE-POINTER).
 * Until the Berkeley select mechanism is done, we must implement transfer
 * tasks as sub-processes.
 * The #ifdef SELECT code is for when that time comes (untested, incomplete).
 */
struct xfer		{
	struct xfer		*x_next;	/* Next in global list */
	struct xfer		*x_runq;	/* Next in runnable list */
	char			x_state;	/* TASK state */
	struct file_handle	*x_fh;		/* File handle of process */
	struct transaction	*x_close;	/* Saved close transaction */
	int			x_bytesize;	/* Bytesize for binary xfers */
	long			x_options;	/* OPEN options flags */
	int			x_flags;	/* Randome state bits */
	time_t			x_atime;	/* Access time to restore */
	time_t			x_mtime;	/* Mod time to restore */
	int			x_fd;		/* File descriptor of file
						   being read or written */
	char			*x_dirname;	/* dirname of filename */
	char			*x_realname;	/* filename */
	char			*x_tempname;	/* While writing */
	int			x_left;		/* Bytes in input buffer */
	int			x_room;		/* Room in output buffer */
	char			*x_bptr;	/* Disk buffer pointer */
	char			*x_pptr;	/* Packet buffer pointer */
	char			x_bbuf[FSBSIZE];	/* Disk buffer */
	struct chpacket		x_pkt;		/* Packet buffer */
#define	x_op			x_pkt.cp_op	/* Packet buffer opcode */
#define x_pbuf			x_pkt.cp_data	/* Packet data buffer */
	char			**x_glob;	/* Files for DIRECTORY */
	char			**x_gptr;	/* Ptr into x_glob vector */
#ifdef SELECT
	struct transaction	*x_work;	/* Queued transactions */
#else
	int			x_pfd;		/* Subprocess pipe file */
	int			x_pid;		/* Subprocess pid */
#endif
};

/*
 * Structure describing each issued command.
 * Created by the command parser (getcmd) and extant while the command is
 * in progress until it is done and responded to.
 */
struct transaction	{
	struct transaction	*t_next;	/* For queuing work on xfers*/
	char			*t_tid;		/* Id. for this transaction */
	struct file_handle	*t_fh;		/* File handle to use */
	struct command		*t_command;	/* Command to perform */
	struct cmdargs		*t_args;	/* Args for this command */
};

/*
 * Structure for each possible command.
 * Used by the parser for argument syntax and holds the actual function
 * which performs the work.
 */
struct command		{
	char			*c_name;	/* Command name */
	void			(*x_func)(struct xfer*, struct transaction*);	/* Function to call */
    void        (*t_func)(struct transaction*);
	int			c_flags;	/* Various bits. See below */
	char			*c_syntax;	/* Syntax description */
};

struct plist	{
	struct plist	*p_next;
	char		*p_name;
	char		*p_value;
};

/*
 * Structure for each "file handle", essentially one side of a
 * bidirectional chaos "data connection".
 */
struct file_handle	{
	struct file_handle	*f_next;	/* Next on global list */
	char			*f_name;	/* Identifier from user end */
	int			f_type;		/* See below */
	int			f_fd;		/* UNIX file descriptor */
	struct xfer		*f_xfer;	/* Active xfer on this fh */
	struct file_handle	*f_other;	/* Other fh of in/out pair */
};


/*
 * Command options.
 */
struct option	{
	char	*o_name;	/* Name of option */
	char	o_type;		/* Type of value */
	long	o_value;	/* Value of option */
	long	o_cant;		/* Bit values of mutually exclusive options*/
};

/*
 * Values for o_type
 */
#define	O_BIT		0	/* Value is bit to turn on in a_options */
#define O_NUMBER	1	/* Value is (SP, NUM) following name */
#define O_EXISTS	2	/* Existence checking keywords */

/*
 * Values for o_value for O_BIT
 */
#define	O_READ		BIT(0)	/* An open is for READing a file */
#define	O_WRITE		BIT(1)	/* An open is for WRITEing a file */
#define	O_PROBE		BIT(2)	/* An open is for PROBEing for existence */
#define	O_CHARACTER	BIT(3)	/* Data will be character (lispm) */
#define	O_BINARY	BIT(4)	/* Binary data */
#define	O_RAW		BIT(5)	/* RAW character transfer - no translation */
#define	O_SUPER		BIT(6)	/* Super-image mode */
#define	O_TEMPORARY	BIT(7)	/* An open file should be temporary */
#define	O_DELETED	BIT(8)	/* Include DELETED files in DIRECTORY list */
#define	O_OLD		BIT(9)	/* Complete for an existing file in COMPLETE */
#define	O_NEWOK		BIT(10)	/* Complete for a possible new file */
#define O_DEFAULT	BIT(11)	/* Choose character unless QFASL */
#define O_DIR    	BIT(12)	/* Used internally - not a keyword option */
#define O_FAST		BIT(13)	/* Fast directory list - no properties */
#define O_PRESERVE	BIT(14)	/* Preserve reference dates - not implemented */
#define O_SORTED	BIT(15) /* Return directory list sorted - we do this */
#define O_PROBEDIR	BIT(16) /* Probe is for the directory, not the file */
#define O_PROPERTIES	BIT(17)	/* To distinguish PROPERTIES from DIRECTORY internally */
/*
 * Values for o_value for O_NUMBER
 */
#define O_BYTESIZE	0
#define O_ESTSIZE	1
#define O_MAXNUMS	2
/*
 * Values for o_value for O_EXISTS
 */
#define O_IFEXISTS	0
#define O_IFNEXISTS	1
#define O_MAXEXS	2
/*
 * Values for O_EXISTS keywords
 */
#define O_XNONE		0	/* Unspecified */
#define O_XNEWVERSION	1	/* Increment version before writing new file */
#define O_XRENAME	2	/* Rename before writing new file */
#define O_XRENDEL	3	/* Rename, then delete before writing new file */
#define O_XOVERWRITE	4	/* Smash the existing file */
#define O_XAPPEND	5	/* Append to existing file */
#define O_XSUPERSEDE	6	/* Don't clobber existing file until non-abort close */
#define O_XCREATE	7	/* Create file is it doesn't exists */
#define O_XERROR	8	/* Signal error */
#define O_XTRUNCATE	9	/* Truncate as well as overwrite on open */
/*
 * Structure of arguments to commands.
 */
#define MAXSTRINGS	3	/* Maximum # of string arguments in any cmd */
struct cmdargs	{
	long			a_options;		/* Option bits */
	char			*a_strings[MAXSTRINGS];/* Random strings */
	char			a_exists[O_MAXEXS];
	long			a_numbers[O_MAXNUMS];	/* Random numbers */
	struct plist		*a_plist;		/* Property list */
};
#define ANULL	((struct cmdargs *)0)

/*
 * String names for above options
 */
struct xoption {
	char	*x_name;
	long	x_value;
	long	x_cant;		/* Which O_EXISTS value can't occur. */
};
/*
 * Structure allocation macros.
 */
#define	salloc(s)	(struct s *)malloc(sizeof(struct s))
#define sfree(sp)	free((char *)sp)
#define NOSTR		((char *)0)
#define NOBLK		((char **)0)
/*
 * Return values from dowork indicating the disposition of the transfer task.
 */
#define X_FLUSH		0	/* Flush the transfer, its all over */
#define X_CONTINUE	1	/* Everything's ok, just keep going */
#define X_HANG		2	/* Wait for more commands before continuing */
#define X_SYNCMARK	3	/* Flush after sending a syncmark */
/*
 * Constants of the protocol
 */
#define	TIDLEN		5		/* Significant length of tid's */
#define FHLEN		5		/* Significant length of fh's */
#define SYNOP		0201		/* Opcode for synchronous marks */
#define ASYNOP		0202		/* Opcode for asynchronous marks */
#define CHAOS_FALSE		"NIL"		/* False value in binary options */
#define CHAOS_TRUE		"T"		/* True value in binary options */
#define QBIN1		((unsigned)0143150)		/* First word of "QFASL" in SIXBIT */
#define QBIN2		071660		/* Second word of "QFASL" */
#define LBIN1		((unsigned)0170023)		/* First word of BIN file */
#define LBIN2LIMIT	100		/* Maximum value of second word of BIN file */

/*
 * Definitions for communication between the control process and transfer
 * processes.  YICK.
 */
/*
 * The structure in which a command is sent to a transfer process.
 */
struct pmesg {
	char		pm_tid[TIDLEN + 1];	/* TIDLEN chars of t->t_tid */
	struct command	*pm_cmd;		/* Actual t_command */
	char		pm_args;		/* 0 = no args, !0 = args */
	long		pm_n;			/* copy of t_args->a_numbers[0] */
	unsigned	pm_strlen;		/* length of string arg. */
};
void write_log(int level, char *fmt, ...);

void dumpbuffer(unsigned char *buf, int cnt);
struct transaction *getwork();
int parseargs(char *args, struct command *c, struct transaction *t);
int getxoption(struct cmdargs *a, long xvalue, char **args);
int string(char **args, char *term, char **dest);
void tinit(struct transaction *t);
void tfree(struct transaction *t);
void afree(struct cmdargs *a);
void pfree(struct plist *p);
char *savestr(char *s);
void ainit(struct cmdargs *a);
void fatal(char *s, ...);
void respond(struct transaction *t, char *results);
void error(struct transaction *t, char *fh, int code);
int syncmark(struct file_handle *f);
void dataconn(struct transaction *t); 
void undataconn(struct transaction *t);
int pwok(struct passwd *p, char *pw);
void login(struct transaction *t);
char * fullname(struct passwd *p);
char * downcase(char *string);
void fileopen(struct transaction *t);
char * tempfile(char *dname);
void getprops(struct transaction *t);
void propopen(struct xfer *x, struct transaction *t);
int propread(struct xfer *x);
void directory(struct transaction *t);
void diropen(struct xfer *ax, struct transaction *t);
int dirread(struct xfer *x);
struct xfer * makexfer(struct transaction *t, long options);
void xcommand(struct transaction *t);
void xflush(struct xfer *x);
void xfree(struct xfer *x);
void fileclose(struct xfer *x, struct transaction *t);
void xclose(struct xfer *ax);
void backfile(char *file);
void filepos(struct xfer *x, struct transaction *t);
void filecontinue(struct xfer *x, struct transaction *t);
void delete(struct transaction *t);
void xrename(struct transaction *t);
int mv(char *from, char *fromdir, char *to, char *todir);
void complete(struct transaction *t);
void incommon(char *old, char *new);
int prefix(char *in, char *new);
void crdir(struct transaction *t);
void expunge(struct transaction *t);
void chngprop(struct transaction *t);
char * getdev(struct stat *s, char *cp);
char * getblock(struct stat *s, char *cp);
char * getspace(struct stat *s, char *cp);
static char * xgetbsize(struct stat *s, char *cp);
char * getbyte(struct stat *s, char *cp);
char * getsize(struct stat *s, char *cp);
char * getmdate(struct stat *s, char *cp);
char * getrdate(struct stat *s, char *cp);
char * getdir(struct stat *s, char *cp);
char * getname(struct stat *s, char *cp);
char * getprot(struct stat *s, char *cp);
char * getsprops(struct stat *s, char *cp);
int putprot(struct stat *s, char *file, char *newprot);
int putname(struct stat *s, char *file, char *newname);
int putmdate(struct stat *s, char *file, char *newtime, struct xfer *x);
int putrdate(struct stat *s, char *file, char *newtime, struct xfer *x);
int parsetime(char *cp, time_t *t);
char * tnum(char *cp, char delim, int *ip);
void jamlower(char *s);
int parsepath(char *path, char **dir, char **real, int blankok);
int dcanon(char *cp, int blankok);
int fherror(struct file_handle *f, int code, char type, char *message);
int dowork(struct xfer *x);
int xbwrite(struct xfer *x);
int xpweof(struct xfer *x);
int xpwrite(struct xfer *x);
int xpread(struct xfer *x);
int startxfer(struct xfer *ax);
void mdatfh(struct xfer *x, struct transaction *t);
void adatfh(struct xfer *x, struct transaction *t);
void delfh(struct xfer *x, struct transaction *t);
void renfh(struct xfer *x, register struct transaction *t);
void from_lispm(struct xfer *x);
void to_lispm(struct xfer *x);
void xcheck();
struct transaction * makecmd(struct command *c, struct file_handle *f);
void sendcmd(struct transaction *t, register struct xfer *x);
void rcvcmd(struct xfer *x);
void finish(int arg);
void from_lispm(struct xfer *x);

#endif

