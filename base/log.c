/*
 * log.c - the logging system
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <execinfo.h>
#include <sched.h>

#include <base/stddef.h>
#include <base/log.h>
#include <base/time.h>
#include <base/thread.h>
#include <asm/ops.h>

#define MAX_LOG_LEN 4096
#define MAX_LOG_LEVEL LOG_DEBUG

#ifdef SUPPRESS_LOG
#undef MAX_LOG_LEVEL
#define MAX_LOG_LEVEL LOG_ERR
#endif

/* log levels greater than this value won't be printed */
int max_loglevel = MAX_LOG_LEVEL;

/* stored here to avoid pushing too much on the stack */
static __thread char buf[MAX_LOG_LEN];

void logk(int level, const char* filename, const char *fmt, ...)
{
	va_list ptr;
	off_t off;
	int cpu;

	if (level > max_loglevel)
		return;

	cpu = sched_getcpu();

	if (likely(base_init_done)) {
		uint64_t us = microtime();
		sprintf(buf, "[%3d.%06d] CPU %02d| THR %d | <%d> [%20s] ",
			(int)(us / ONE_SECOND), (int)(us % ONE_SECOND),
			cpu, thread_gettid(), level, filename);
	} else {
		sprintf(buf, "CPU %02d| <%d> [%10s] ", cpu, level, filename);
	}

	off = strlen(buf);
	va_start(ptr, fmt);
	vsnprintf(buf + off, MAX_LOG_LEN - off, fmt, ptr);
	va_end(ptr);
	/* stderr, not stdout: this runs on the handler thread (and other
	 * internal threads) as well as app threads, and puts()/stdout share
	 * a single lock with whatever the traced application itself prints
	 * to stdout. If the app is holding that lock while blocked on a page
	 * fault (e.g. mid printf, waiting on a malloc that faulted), and the
	 * handler thread needs this same lock just to log its own startup
	 * message before it can reach its fault-servicing loop, that's a
	 * real deadlock - confirmed via gdb while debugging SPEC mcf, which
	 * hits this deadlock on essentially every run since its first
	 * statement is a version banner print. */
	fprintf(stderr, "%s\n", buf);

	if (level <= LOG_ERR)
		fflush(stderr);
}

#define MAX_CALL_DEPTH	256
void logk_backtrace(void)
{
	void *buf[MAX_CALL_DEPTH];
	const int calls = backtrace(buf, ARRAY_SIZE(buf));
	backtrace_symbols_fd(buf, calls, 2);
}

void logk_bug(bool fatal, const char *expr,
	      const char *file, int line, const char *func)
{
	logk(LOG_EMERG, file, "%s: %s:%d ASSERTION '%s' FAILED IN '%s'",
	     fatal ? "FATAL" : "WARN", file, line, expr, func);
	logk_backtrace();

	if (fatal)
		init_shutdown(EXIT_FAILURE);
}
