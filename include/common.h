#ifndef COMMON_H
#define COMMON_H

/* =====================================================================
 * common.h - shared contract for logd / logctl / loggen
 *
 * Group 6, COMP 3659 Project 2. Agreed Wed Nov 18.
 *
 * Every .c file in this project includes this header and nothing else
 * from another owner's code. If you need to change a signature here,
 * tell the group first - it breaks other people's builds.
 *
 *   A (Justin): queue.c dispatcher.c workers.c main.c
 *   B (Paul):   rules.c output.c control.c
 *   C (Josen):  logctl.c gen.c harness.c producer.c
 * =====================================================================
 */

/* =====================================================================
 * LOCK ORDER - read this before you add a lock anywhere
 *
 *      rules lock  ->  queue lock  ->  per-file lock
 *
 * Acquire in that order, always. Release in reverse.
 * NEVER hold the queue lock while writing to a file.
 *
 * A fixed global lock order breaks the circular-wait condition, which
 * is the deadlock-freedom argument in the report. If you find yourself
 * wanting to take these out of order, the design is wrong - fix the
 * design, not the order.
 * =====================================================================
 */

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

/* ---------------------------------------------------------------------
 * Paths. Hardcode these nowhere else.
 * ------------------------------------------------------------------ */
#define FIFO_PATH   "/tmp/logd.fifo"
#define SOCK_PATH   "/tmp/logd.sock"
#define RULES_PATH  "config/rules.conf"
#define EVENTS_PATH "defs/events.def"
#define LOG_DIR     "logs"

/* ---------------------------------------------------------------------
 * Message format:  LEVEL|source|message\n
 *
 * MAX_LINE is the maximum on-the-wire length INCLUDING the trailing
 * newline. Longer lines are truncated to MAX_LINE with the newline
 * preserved. Buffers hold MAX_LINE + 1 so there is room for a NUL.
 *
 * MAX_LINE must stay <= PIPE_BUF (4096 on Linux) so that a producer's
 * single write() to the FIFO is atomic and concurrent producers can
 * never interleave halves of a line.
 * ------------------------------------------------------------------ */
#define MAX_LINE    512
#define MAX_SOURCE   64
#define MAX_PATH_LEN 256

/* ---------------------------------------------------------------------
 * Severity levels. Order matters: rules compare with >=, so a rule with
 * min-level WARN matches WARN and ERROR but not INFO or DEBUG.
 * ------------------------------------------------------------------ */
typedef enum {
    LVL_INVALID = -1,
    LVL_DEBUG   =  0,
    LVL_INFO    =  1,
    LVL_WARN    =  2,
    LVL_ERROR   =  3
} level_t;

#define LVL_COUNT 4

/* Implemented once in common.c so all three of us parse identically. */
level_t     level_parse(const char *s);  /* LVL_INVALID if unrecognized */
const char *level_name(level_t lvl);     /* "DEBUG".."ERROR", "?" if bad */

/*
 * Split a raw log line into its source and level. Non-destructive: the
 * caller keeps the original line and writes it to the log file verbatim.
 *
 * Returns 0 on success, -1 if the line is malformed (missing separators,
 * unknown level, empty source). Malformed lines are counted and dropped.
 */
int msg_parse(const char *line, char *source_out, size_t source_sz,
              level_t *lvl_out);

/* =====================================================================
 * A: bounded queue  (queue.c)
 *
 * Array ring buffer, one mutex, two condition variables. Producers are
 * the dispatcher thread; consumers are the worker pool.
 * ===================================================================== */

#define QUEUE_CAP       1024
#define DEFAULT_WORKERS    4
#define MAX_WORKERS       64   /* upper bound on -w N, sizes the tid array */

typedef struct {
    char line[MAX_LINE + 1];
} msg_t;

typedef struct {
    msg_t           buf[QUEUE_CAP];
    size_t          head;        /* next slot to pop  */
    size_t          tail;        /* next slot to push */
    size_t          count;       /* items currently queued */
    int             shutdown;    /* set by queue_shutdown() */
    pthread_mutex_t lock;        /* guards every field above */
    pthread_cond_t  item_avail;  /* workers sleep here when count == 0 */
    pthread_cond_t  slot_avail;  /* dispatcher sleeps here when count == CAP */
} queue_t;

void queue_init(queue_t *q);
void queue_destroy(queue_t *q);

/*
 * Blocks while the queue is full. Copies at most MAX_LINE bytes.
 * Returns 1 if the line was queued, 0 if the queue is shutting down.
 */
int queue_push(queue_t *q, const char *line);

/*
 * Blocks while the queue is empty. Returns 1 and fills *out on success.
 * Returns 0 only when the queue is shutting down AND drained - workers
 * use that as their exit condition, so they always drain first.
 */
int queue_pop(queue_t *q, msg_t *out);

/*
 * Sets the shutdown flag and broadcasts on both condition variables so
 * every blocked thread wakes up and sees it. Idempotent.
 */
void queue_shutdown(queue_t *q);

/* Current depth, for `logctl status`. Takes the queue lock. */
size_t queue_depth(queue_t *q);

/* =====================================================================
 * B: rules table  (rules.c)
 *
 * Protected by the textbook first readers-writers solution
 * (rw_mutex / mutex / read_count). Workers are readers; SIGHUP reload
 * and `logctl set ...` are writers.
 * ===================================================================== */

#define MAX_RULES 64

/*
 * One rule per line in rules.conf:
 *     <source-or-*>  <min-level>  <output-file>
 * e.g.
 *     auth   WARN   logs/auth.log
 *     *      INFO   logs/all.log
 *
 * First match wins. A line whose source matches no rule is dropped.
 */
typedef struct {
    char    source[MAX_SOURCE];      /* "*" matches any source */
    level_t min_level;
    char    path[MAX_PATH_LEN];
} rule_t;

int rules_init(const char *path);

/*
 * Re-read the rules file. Takes the writer lock. On a parse error the
 * old table is kept unchanged. Returns rule count on success, -1 on error.
 * Called from the dispatcher after SIGHUP, and by `logctl reload`.
 */
int rules_reload(const char *path);

/*
 * Returns the index of the first matching rule, or -1 to drop the line.
 * Takes the reader lock. This is the hot path - workers call it once
 * per message.
 */
int rules_match(const char *source, level_t lvl);

/*
 * Live settings change from logctl. Takes the writer lock.
 * Recognized keys: "max_size" (bytes), "level" (DEBUG|INFO|WARN|ERROR).
 * Returns 0 on success, -1 on bad key or value.
 */
int rules_set(const char *key, const char *value);

/*
 * Readers-writers primitives. Declared here so the lock order above is
 * enforceable by inspection; only rules.c and control.c should call them.
 */
void rules_read_begin(void);
void rules_read_end(void);
void rules_write_begin(void);
void rules_write_end(void);

/* =====================================================================
 * B: live configuration
 *
 * Read under the rules READ lock, modified under the rules WRITE lock.
 * Defined in rules.c.
 * ===================================================================== */

#define DEFAULT_MAX_SIZE (64 * 1024)   /* rotate at 64 KB unless told otherwise */

typedef struct {
    int     enabled;     /* `logctl off` drops lines without writing them */
    size_t  max_size;    /* rotate when an open log file reaches this size */
    level_t min_level;   /* daemon-wide floor, applied before rules_match */
} daemon_cfg_t;

extern daemon_cfg_t g_cfg;

/* =====================================================================
 * B: log file output + rotation  (output.c)
 * ===================================================================== */

int output_init(void);

/*
 * Append one complete line to the file named by rule index `idx`.
 * Holds only that file's mutex - never the queue lock (see LOCK ORDER).
 *
 * After writing, fstat() the fd; if size >= g_cfg.max_size, rotate:
 * unlink x.log.1 if present, rename x.log -> x.log.1, reopen x.log.
 *
 * Returns 0 on success, -1 on I/O error.
 */
int output_write(int idx, const char *line);

/* Flush and close every open log file. Called once during shutdown. */
void output_close_all(void);

/* Total lines written since start, for `logctl status`. */
unsigned long output_lines_written(void);

/* =====================================================================
 * B: control command handler  (control.c)
 *
 * Protocol: the client sends ONE line. The daemon replies with zero or
 * more informational lines, then a final line that is exactly "OK" or
 * "ERR <reason>". The client reads until it sees that final line.
 *
 * Commands: status | on | off | set max_size <bytes> |
 *           set level <LEVEL> | reload
 * ===================================================================== */

#define REPLY_MAX 1024

/*
 * Handles one command line (newline already stripped) and writes the
 * complete reply, terminator included, into `reply`.
 * Returns 0 if the command was understood, -1 otherwise. Either way
 * `reply` is filled with something safe to send.
 */
int control_handle(const char *cmd, char *reply, size_t reply_sz);

/* =====================================================================
 * A: signal flags  (main.c)
 *
 * Handlers do NOTHING but assign to these. No printf, no malloc, no
 * locking - none of that is async-signal-safe. The dispatcher checks
 * both flags on every pass of its poll() loop.
 * ===================================================================== */

extern volatile sig_atomic_t g_shutdown;   /* set by SIGINT / SIGTERM */
extern volatile sig_atomic_t g_reload;     /* set by SIGHUP           */

/* =====================================================================
 * A: daemon internals shared between main.c / dispatcher.c / workers.c
 * ===================================================================== */

extern queue_t g_queue;
extern int     g_worker_count;   /* from -w N, default DEFAULT_WORKERS */

/* dispatcher.c: owns the poll() loop. Returns when g_shutdown is set.
 * Week 1: FIFO only. Week 2 gains a listen_fd parameter for the control
 * socket, which is why main.c opens the fds rather than the dispatcher. */
void dispatcher_run(int fifo_fd);

/* workers.c: pthread entry point. arg is &g_queue. */
void *worker_main(void *arg);

#endif /* COMMON_H */