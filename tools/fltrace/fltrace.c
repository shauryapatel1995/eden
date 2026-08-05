/**
 * fltrace.c - Memory interposition library to forward 
 * all heap allocations to UFFD-registered memory and 
 * kick off a handler thread to serve them.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <jemalloc/jemalloc.h>

#include "base/assert.h"
#include "base/atomic.h"
#include "base/log.h"
#include "base/mem.h"
#include "base/realmem.h"
#include "rmem/api.h"
#include "rmem/common.h"
#include "rmem/region.h"

/* concurrent ld_preload in multiple procs; no reason not to allow it yet, 
 * this might change if we introduce rdma-backed memory */
#define ALLOW_CONCURRENT_TRACING

/**
 * Defs 
 */
enum init_state {
    NOT_STARTED = 0,
    INITIALIZED = 1,
    INIT_STARTED = 2,
    INIT_FAILED = 3
};

/* from base lib */
extern int time_init(void);

/* stats thread */
extern int start_stats_thread(int stats_core);

/* State */
__thread bool __from_internal_jemalloc = false;
__thread bool __init_in_progress = false;
static atomic_t rmlib_state = ATOMIC_INIT(NOT_STARTED);
unsigned long max_memory_mb = 1;
int shm_id;
int samples_per_sec = -1;
static int fltrace_exit_status = 0;

#ifdef RDMA_LINKED
/* interpose exit() only to capture the real status the process is exiting
 * with - finish()'s destructor needs it to _exit() with the right code
 * instead of always reporting success (see finish() for why it hard-exits
 * at all under RDMA_LINKED). Immediately forwards to the real exit(), so
 * normal exit() behavior (atexit handlers, destructors) is unaffected up
 * until finish() itself decides to bypass the rest of them. */
void exit(int status)
{
    void (*real_exit)(int) = dlsym(RTLD_NEXT, "exit");
    fltrace_exit_status = status;
    real_exit(status);
    __builtin_unreachable();
}
#endif

/**
 * We need modified versions of logging calls that do not call 
 * malloc internally to avoid recursive behavior. 
 * We will only use these functions for logging in this file.
 */
#define ft_log(fmt, ...)                                    \
  do {                                                      \
    fprintf(stderr, "[%s][%s:%d]: " fmt "\n", __FILE__,     \
            __func__, __LINE__, ##__VA_ARGS__);             \
  } while (0)
#define ft_bug_on(cond, fmt, ...)                           \
    do {                                                    \
        if (cond) {                                         \
            ft_log(fmt, ##__VA_ARGS__);                     \
            exit(1);                                        \
        }                                                   \
    } while (0)

#ifdef SUPPRESS_LOG
#define ft_log_suppressible(fmt, ...) do {} while (0)
#else
#define ft_log_suppressible ft_log
#endif

/* log levels */
#define ft_log_err  ft_log
#define ft_log_info ft_log_suppressible
#define ft_log_warn ft_log_suppressible
#ifdef DEBUG
#define ft_log_debug ft_log_suppressible
#else
#define ft_log_debug(fmt, ...) do {} while (0)
#endif

/**
 * Helpers 
 */
int parse_numeric_env_setting(const char* name, long int* parsed_num)
{
    char *val_str;

    val_str = getenv(name);
    if (val_str == NULL)
        return 1;

    /* Hack: fix a bug wheren env variable has non-printable chars at start */
    while (!isalnum(val_str[0]) && val_str[0] != 0)
        val_str++;

    *parsed_num = atoll(val_str);
    ft_log_info("parsed env setting: %s=%ld", name, *parsed_num);
    return 0;
}

int parse_env_settings()
{
    long int val;
    bool local_memory_set;
    char *backend_str;

    /* parse backend (default: local). rdma_init() reads its own server
     * info from RDMA_RACK_CNTRL_IP/RDMA_RACK_CNTRL_PORT env vars, so
     * nothing else needs to change here to point fltrace.so at a remote
     * memory server instead of local memory */
    rmbackend_type = RMEM_BACKEND_LOCAL;
    backend_str = getenv("FLTRACE_RMEM_BACKEND");
    if (backend_str != NULL && strcmp(backend_str, "rdma") == 0)
        rmbackend_type = RMEM_BACKEND_RDMA;
    ft_log_info("using %s rmem backend",
        (rmbackend_type == RMEM_BACKEND_RDMA) ? "rdma" : "local");

    /* parse local memory */
    local_memory_set = false;
    if (parse_numeric_env_setting("FLTRACE_LOCAL_MEMORY_BYTES", &val) == 0) {
        local_memory = val;
        local_memory_set = true;
    }
    if (parse_numeric_env_setting("FLTRACE_LOCAL_MEMORY_MB", &val) == 0) {
        local_memory = val * 1024 * 1024;
        local_memory_set = true;
    }
    if (!local_memory_set || local_memory < CHUNK_SIZE) {
        ft_log_err("ERROR! local mem must be set (FLTRACE_LOCAL_MEMORY_BYTES or"
            " FLTRACE_LOCAL_MEMORY_MB env) with at least %d bytes", CHUNK_SIZE);
        return 1;
    }

    /* parse number of handlers */
    if (parse_numeric_env_setting("FLTRACE_NHANDLERS", &val) == 0)
        nhandlers = val;

    /* parse max backing memory */
    if (parse_numeric_env_setting("FLTRACE_MAX_MEMORY_MB", &val) == 0)
        max_memory_mb = val;

    /* parse sampling rate */
    if (parse_numeric_env_setting("FLTRACE_MAX_SAMPLES_PER_SEC", &val) == 0)
        samples_per_sec = val;

    return 0;
}

/**
 * Some wrappers for RMem API
 */

void *rmlib_rmmap(void *addr, size_t length, int prot, 
    int flags, int fd, off_t offset)
{
    void *p = NULL;

    if ((fd != -1) && (flags & MAP_ANONYMOUS)) {
        ft_log_err("ERROR! bad mmap args: fd=%d, flags=%d", fd, flags);
        exit(1);
    }

    if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_ANON) || (prot & PROT_EXEC) 
            || (flags & (MAP_STACK | MAP_FIXED | MAP_DENYWRITE))
            || (addr && !within_memory_region(addr)))
    {
        log_warn_ratelimited("WARNING! non-anon mmap");
        p = real_mmap(addr, length, prot, flags, fd, offset);
    } else {
        /* we don't support these flags */
        assertz(prot & PROT_EXEC);
        assertz(flags & MAP_STACK);
        assertz(flags & MAP_FIXED);
        assertz(flags & MAP_DENYWRITE);
        assert(fd == -1);
        assert(length);
        ft_log_debug("%s - using rmalloc", __func__);
        p = rmalloc(length);
    }
    return p;
}

/**
 * Main Initialization - every external (i.e., coming from the
 * application) memory alloc/map/free call we interpose on in this
 * library will call this function first to initialize resources or
 * wait while someone else does it.
 *
 * We could initialize rmlib in a constructor, such as: static
 * __attribute__((constructor)) void __init__(void) but that would be
 * incorrect because the contructor is called before main, but malloc
 * can be called during other libraries initializations.
 *
 * Note on jemalloc: jemalloc relies on libc mmap, which we interpose.
 * We forward all external memory calls to jemalloc and handle its
 * allocation calls in our mmap shim.
 *
 * This whole interposition is infested with potential infinite loops
 * so tread carefully. E.g., To initialize real mmap, we use dlopen
 * which calls malloc internally. Similarly, printf and other logging
 * calls during init may call malloc too.
 */
static bool init(bool init_start_expected)
{
    bool ret, status;
    int r, oldval, initd, nchars;
    unsigned long nslabs;
    char exepath[200];

    /* inf loop; a thread came back here during init */
    if (__init_in_progress) {
        ft_log_err("ERROR! init called recursively");
        exit(1);
    }

again:
    /* check rmlib status */
    initd = atomic_read(&rmlib_state);
    switch (initd)
    {
        case NOT_STARTED:
            /* continue to init */
            if (init_start_expected) {
                ft_log_err("ERROR! init already expected");
                exit(1);
            }
            break;
        case INIT_STARTED:
            /* wait for init to finish */
            cpu_relax();
            goto again;
            break;
        case INIT_FAILED:
            return false;
        case INITIALIZED:
            return true;
        default:
            ft_bug_on(true, "unknown case");  /*unknown*/
    }

    /* claim the one to be initing */
    oldval = atomic_cmpxchg_val(&rmlib_state, NOT_STARTED, INIT_STARTED);
    ft_log_debug("CAS ret=%d", oldval);
    if (oldval != NOT_STARTED)
        /* someone else started, check again on my next action */
        goto again;

    /* i started init */
    __init_in_progress = true;

    /* print tracing process info */
    nchars = readlink("/proc/self/exe", exepath, 200);
    if (nchars < 0) nchars = 0;
    ft_log_info("profiling process: %d exe path: %.*s",
        getpid(), (int) nchars, exepath);

#ifndef ALLOW_CONCURRENT_TRACING
    /* check for fork'ed or other processes that inherit LD_PRELOAD */
    key_t key;
    int shmid;

    key = ftok("rmem_rmlib", 65);
    shmid = shmget(key, 1024, 0666 | IPC_CREAT | IPC_EXCL);
    if (shmid < 0) {
        ft_log_warn("failed to create new shmid, some other process or parent" 
            "process may already be running with rmlib. errno: %d", errno);
        /* use libc for other processes */
        goto error;
    }

    /* write something to shmid for debugging */
    shm_id = shmid;
    char *str = (char *)shmat(shmid, (void *)0, 0);
    sprintf(str, "hello from pid %d", getpid());
    shmdt(str);
#endif

    /* get settings from env */
    r = parse_env_settings();
    if (r) {
        ft_log_err("failed to parse env settings");
        goto error;
    }

    /* init some base library things */
    ft_log_debug("calling time init");
    r = time_init();
    if (r)  goto error;

    /* init rmem (backend already chosen in parse_env_settings()) */
    ft_log_debug("calling rmem init");
    rmem_enabled = true;
    eviction_threshold = 1;
    nslabs= max_memory_mb * 1024L * 1024L / RMEM_SLAB_SIZE;
    r = rmem_common_init(nslabs, -1, -1, samples_per_sec);
    if (r)  goto error;

    /* kick-off stats thread */
    ft_log_debug("starting stats thread");
    start_stats_thread(-1);

    /* done initializing */
    ret = atomic_cmpxchg(&rmlib_state, INIT_STARTED, INITIALIZED);
    if(!ret) goto error;
    status = true;
    goto out;

error:
    ft_log_warn("couldn't init remote memory; reverting to libc");
    ret = atomic_cmpxchg(&rmlib_state, INIT_STARTED, INIT_FAILED);
    ft_bug_on(!ret, "atomic cmpxchg failed");
    status = false;
    goto out;

out:
    __init_in_progress = false;
    return status;
}

/**
 *  Interface functions
 */

void *malloc(size_t size)
{
    bool from_runtime;
    void* retptr;

    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s], size=%lu, from-runtime=%d from-jemalloc=%d",
        __func__, size, from_runtime, __from_internal_jemalloc);

    if (from_runtime) {
        ft_log_debug("%s from runtime, using libc", __func__);
        retptr = real_malloc(size);
        goto out;
    }

    /* rmlib status */
    if (!init(false)) {
        ft_log_debug("%s not initialized, using libc", __func__);
        retptr = real_malloc(size);
        goto out;
    }

    /* application malloc */
    __from_internal_jemalloc = true;
    ft_log_debug("using je_malloc");
    retptr = rmlib_je_malloc(size);
    __from_internal_jemalloc = false;

out:
    ft_log_debug("[%s] return=%p", __func__, retptr);
    if (!from_runtime)
        RUNTIME_EXIT();
    return retptr;
}

void free(void *ptr)
{
    bool from_runtime;

    if (ptr == NULL)
        return;

    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s] ptr=%p from-runtime=%d from-jemalloc=%d", __func__,
        ptr, from_runtime, __from_internal_jemalloc);

    if (from_runtime) {
        ft_log_debug("[%s] from runtime, using libc", __func__);
        real_free(ptr);
        goto out;
    }

    /* rmlib status */
    if (!init(true)) {
        ft_log_debug("[%s] not initialized, using libc", __func__);
        real_free(ptr);
        goto out;
    }

    /* if we are here, this should be a remote pointer. just warn for now */
    if (!within_memory_region(ptr)) {
        ft_log_debug("[%s] WARN - unexpected real ptr from app", __func__);
        real_free(ptr);
        goto out;
    }

    /* application free */
    __from_internal_jemalloc = true;
    rmlib_je_free(ptr);
    __from_internal_jemalloc = false;

out:
    ft_log_debug("[%s] return", __func__);
    if (!from_runtime)
        RUNTIME_EXIT();
}

void *realloc(void *ptr, size_t size)
{
    void *retptr;
    bool from_runtime;

    if (ptr == NULL) 
        return malloc(size);

    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s] ptr=%p, size=%lu, from-runtime=%d from-jemalloc=%d",
        __func__, ptr, size, from_runtime, __from_internal_jemalloc);

    if (from_runtime) {
        ft_log_debug("%s from runtime, using libc", __func__);
        retptr = real_realloc(ptr, size);
        goto out;
    }

    /* rmlib status */
    if (!init(true)) {
        ft_log_debug("%s not initialized, using libc", __func__);
        retptr = real_realloc(ptr, size);
        goto out;
    }
    
    /* application realloc */
    __from_internal_jemalloc = true;
    retptr = rmlib_je_realloc(ptr, size);
    __from_internal_jemalloc = false;

out:
    ft_log_debug("[%s] return=%p", __func__, retptr);
    if (!from_runtime)
        RUNTIME_EXIT();
    return retptr;
}

void *calloc(size_t nitems, size_t size)
{
    void *retptr;
    bool from_runtime;

    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s] number=%lu, size=%lu, from-runtime=%d", 
        __func__, nitems, size, from_runtime);

    if (from_runtime) {
        ft_log_debug("%s from runtime, using libc", __func__);
        retptr = real_calloc(nitems, size);
        goto out;
    }

    /* rmlib status */
    if (!init(false)) {
        ft_log_debug("%s not initialized, using libc", __func__);
        retptr = real_calloc(nitems, size);
        goto out;
    }

    /* application calloc */
    __from_internal_jemalloc = true;
    retptr = rmlib_je_calloc(nitems, size);
    __from_internal_jemalloc = false;

out:
    ft_log_debug("[%s] return=%p", __func__, retptr);
    if (!from_runtime)
        RUNTIME_EXIT();
    return retptr;
}

void *__internal_aligned_alloc(size_t alignment, size_t size)
{
    void *retptr;
    bool from_runtime;

    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s] alignment=%lu, size=%lu, from-runtime=%d", 
        __func__, alignment, size, from_runtime);

    if (from_runtime) {
        ft_log_debug("%s from runtime, using libc", __func__);
        retptr = real_memalign(alignment, size);
        goto out;
    }

    /* rmlib status */
    if (!init(false)) {
        ft_log_debug("%s not initialized, using libc", __func__);
        retptr = real_memalign(alignment, size);
        goto out;
    }

    /* application aligned alloc */
    __from_internal_jemalloc = true;
    retptr = rmlib_je_aligned_alloc(alignment, size);
    __from_internal_jemalloc = false;

out:
    ft_log_debug("[%s] return=%p", __func__, retptr);
    if (!from_runtime)
        RUNTIME_EXIT();
    return retptr;
}

int posix_memalign(void **ptr, size_t alignment, size_t size)
{
    ft_log_debug("[%s] ptr=%p, alignment=%lu, size=%lu", 
        __func__, ptr, alignment, size);
    /* TODO: need to check alignment, check return values */
    *ptr = __internal_aligned_alloc(alignment, size);
    return 0;
}

void *memalign(size_t alignment, size_t size)
{
    ft_log_debug("[%s] alignment=%lu, size=%lu", __func__, alignment, size);
    return __internal_aligned_alloc(alignment, size);
}

void *aligned_alloc(size_t alignment, size_t size)
{
    ft_log_debug("[%s] alignment=%lu, size=%lu", __func__, alignment, size);
    return __internal_aligned_alloc(alignment, size);
}

size_t malloc_usable_size(void * ptr)
{
    size_t size;
    bool from_runtime;

    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s] ptr %p", __func__, ptr);

    if (from_runtime) {
        ft_log_debug("%s from runtime, using libc", __func__);
        size = real_malloc_usable_size(ptr);
        goto out;
    }

    /* rmlib status */
    if (!init(false)) {
        ft_log_debug("%s not initialized, using libc", __func__);
        size = real_malloc_usable_size(ptr);
        goto out;
    }

    /* application aligned alloc */
    __from_internal_jemalloc = true;
    size = rmlib_je_malloc_usable_size(ptr);
    __from_internal_jemalloc = false;

out:
    ft_log_debug("[%s] return=%ld", __func__, size);
    if (!from_runtime)
        RUNTIME_EXIT();
    return size;
}

/**
 * Memory management functions (sys/mman.h).
 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    void *retptr;
    bool from_runtime;

    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s] addr=%p,length=%lu,prot=%d,flags=%d,fd=%d,offset=%ld,from-"
    "runtime=%d", __func__, addr, length, prot, flags, fd, offset, from_runtime);

    /* First check for calls coming from jemalloc. These allocations are 
     * meant to be forwarded to remote memory; the tool must have been inited
     * by now as jemalloc calls are triggered by our own calls after init */
    if (__from_internal_jemalloc) {
        ft_log_debug("internal jemalloc mmap, fwd to RLib: addr=%p", addr);
        if (rmlib_state.cnt != INITIALIZED) {
            ft_log_err("ERROR! rmlib not initialized before jemalloc mmap");
            exit(1);
        }
        retptr = rmlib_rmmap(addr, length, prot, flags, fd, offset);
        goto out;
    }

    /* mmap coming directly from runtime */
    if (from_runtime) {
        ft_log_debug("%s from runtime, using real mmap", __func__);
        retptr = real_mmap(addr, length, prot, flags, fd, offset);
        goto out;
    }

    /* mmap coming from the app, initialize */
    if (!init(false)) {
        ft_log_debug("%s not initialized, using real mmap", __func__);
        retptr = real_mmap(addr, length, prot, flags, fd, offset);
        goto out;
    }

    ft_log_debug("%s directly from the app, fwd to RLib", __func__);
    retptr = rmlib_rmmap(addr, length, prot, flags, fd, offset);

out:
    ft_log_debug("[%s] return=%p", __func__, retptr);
    if (!from_runtime)
        RUNTIME_EXIT();
    return retptr;
}

int munmap(void *ptr, size_t length)
{
    int ret;
    bool from_runtime;
    enum init_state initd;

    if (!ptr) 
        return 0;
    
    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s] ptr=%p, length=%lu, from-runtime=%d", __func__, ptr, 
        length, from_runtime);

    /* check init status */
    initd = atomic_read(&rmlib_state);

    /* First check for calls coming from jemalloc. These deallocations are 
     * meant to be forwarded to remote memory; the tool must have been init'd 
     * by now as jemalloc calls are triggered by our own calls after init */
    if (__from_internal_jemalloc) {
        ft_log_debug("internal jemalloc munmap, fwd to RLib: addr=%p", ptr);
        BUG_ON(initd != INITIALIZED);
        assert(within_memory_region(ptr));
        ret = rmunmap(ptr, length);
        goto out;
    }

    if (from_runtime) {
        ft_log_debug("%s from runtime, using real munmap", __func__);
        assert(!within_memory_region(ptr));
        ret = real_munmap(ptr, length);
        goto out;
    }

    /* from the app, check on rmlib status again */
    if (!init(true)) {
        ft_log_debug("[%s] not initialized, using libc", __func__);
        ret = real_munmap(ptr, length);
        goto out;
    }

    /* if we are here, this should be a remote pointer. just warn for now */
    if (!within_memory_region(ptr)) {
        ft_log_debug("[%s] WARN - unexpected real ptr from app", __func__);
        ret = real_munmap(ptr, length);
        goto out;
    }

    ft_log_debug("munmap directly from the app, fwd to RLib: addr=%p", ptr);
    ret = rmunmap(ptr, length);

out:
    ft_log_debug("[%s] return=%d", __func__, ret);
    if (!from_runtime)
        RUNTIME_EXIT();
    return ret;
}

int madvise(void *addr, size_t length, int advice)
{
    int ret;
    bool from_runtime;
    enum init_state initd;

    from_runtime = IN_RUNTIME();
    RUNTIME_ENTER();

    ft_log_debug("[%s] addr=%p, size=%lu, advice=%d, from-runtime=%d from-je=%d", 
        __func__, addr, length, advice, from_runtime, __from_internal_jemalloc);
    if (advice == MADV_DONTNEED)    ft_log_debug("MADV_DONTNEED flag");
    if (advice == MADV_HUGEPAGE)    ft_log_debug("MADV_HUGEPAGE flag");
    if (advice == MADV_FREE)        ft_log_debug("MADV_FREE flag");

        /* check init status */
    initd = atomic_read(&rmlib_state);

    /* First check for calls coming from jemalloc. These deallocations are 
     * meant to be forwarded to remote memory; the tool must have been init'd 
     * by now as jemalloc calls are triggered by our own calls after init */
    if (__from_internal_jemalloc) {
        ft_log_debug("internal jemalloc madvise, fwd to RLib: addr=%p", addr);
        BUG_ON(initd != INITIALIZED);
        assert(within_memory_region(addr));
        ret = rmadvise(addr, length, advice);
        goto out;
    }

    if (from_runtime) {
        ft_log_debug("%s from runtime, using real madvise", __func__);
        ret = real_madvise(addr, length, advice);
        goto out;
    }

    /* from the app, check on rmlib status again */
    if (!init(false)) {
        ft_log_debug("[%s] not initialized, using libc", __func__);
        ret = real_madvise(addr, length, advice);
        goto out;
    }

    /* if we are here, this should be a remote pointer. just warn for now */
    if (!within_memory_region(addr)) {
        ft_log_debug("[%s] WARN - unexpected real ptr from app", __func__);
        ret = real_madvise(addr, length, advice);
        goto out;
    }

    ft_log_debug("madvise directly from the app, fwd to RLib: addr=%p", addr);
    ret = rmadvise(addr, length, advice);

out:
    ft_log_debug("[%s] return=%d", __func__, ret);
    if (!from_runtime)
        RUNTIME_EXIT();
    return ret;
}

#if 0
/* others? */
void *mremap(void *old_addr, size_t old_size, size_t new_size, int flags,
                         ... /* void *new_address */) {
    ft_log_debug("addr=%p,old_size=%lu,new_size=%lu,flags=%d,from-runtime=%d",
       old_addr, old_size, new_size, flags, from_runtime);
    return 0;
}
#endif

static __attribute__((constructor)) void __init__(void)
{
    ft_log_debug("ftrace constructor");
}

static __attribute__((destructor)) void finish(void)
{
    int i;

    ft_log_debug("ftrace destructor");
    /* NOTE: ideally we should free all remote memory resources
     * with rmem_common_destroy() here but since we assume that
     * the program begins in "application" mode rather than in
     * "runtime" mode, we send all initial (even before main())
     * allocations to remote memory; hence we need the remote
     * memory running even during destroy - so we don't tear down
     * the backend/regions/allocator here, only the handler threads. */

    /* stop and join every handler thread (same as the first step of
     * rmem_common_destroy()) instead of just sleeping and hoping they're
     * done: destructors for other shared libraries run right after this
     * one returns (as part of the same _dl_fini() pass), and a handler
     * thread can still be mid-completion at any point - e.g. finishing an
     * in-flight read that was posted but hadn't completed when main()
     * returned, which under DO_PREFETCH always re-scans the page for new
     * prefetch candidates and calls into XGBoost. A fixed sleep(1) doesn't
     * guarantee that's done, and did happen to mask this for years - local
     * backend reads/prefetches complete near-instantly, so 1 real second
     * was always enough dead time in practice. RDMA's network latency
     * breaks that assumption: a completion can genuinely still be in
     * flight past that window, and if some other library's destructor
     * (e.g. XGBoost's, or anything it depends on) has already run and torn
     * down global state that the handler thread then touches, it's a
     * genuine, silent use-after-free/segfault rather than a merely wasted
     * sleep. */
    /* rmem_enabled is set before rmem_common_init() runs and isn't reset
     * if it later fails (e.g. the backend rejecting the initial memory
     * request), so handlers can still be NULL here - guard on both */
    if (rmem_enabled && handlers != NULL) {
        for (i = 0; i < nhandlers; i++)
            stop_rmem_handler_thread(handlers[i]);

        /* close the backend connection (e.g. rdma_destroy() tearing down
         * the RDMA CM connection/QPs) now that nothing is using it - just
         * the connection, not the rest of rmem (regions/allocator/tcaches
         * stay up for the same "other destructors may still free() into
         * rmem" reason as above). */
        rmbackend->destroy();
    }

#ifdef RDMA_LINKED
    /* RDMA=1 builds link -lrdmacm/-libverbs, which pull in libnl-3/
     * libnl-route as transitive deps. Something in libnl-route's own
     * shared-library destructor (__trans_list_clear) hangs indefinitely
     * during _dl_fini() on this machine - confirmed to be a pre-existing
     * bug in the system's netlink libraries (or their interaction with
     * glibc's exit path), unrelated to Eden: it reproduces identically
     * with the local backend as long as these libs are linked at all, and
     * disappears entirely when they aren't (the non-RDMA default build).
     * We don't control that library's source, so rather than trying to
     * fix it, skip the rest of libc's destructor chain outright - we've
     * already done the cleanup we actually need above (stopped handler
     * threads, closed the backend connection), and by this point the
     * application's own output is already flushed (confirmed: it shows up
     * intact in every run's log, including ones that crashed or hung
     * later in this exact destructor chain), so there's nothing left to
     * lose by hard-exiting here instead of letting _dl_fini() continue.
     *
     * Uses fltrace_exit_status (captured by the exit() interposition
     * above) rather than hardcoding 0 - this destructor also runs on
     * failure paths (e.g. BUG()-triggered aborts from rmem code, which do
     * go through exit() since init_shutdown()'s call resolves to our own
     * interposed exit() at link time), and hardcoding 0 would silently
     * report those as success. */
    fflush(NULL);
    _exit(fltrace_exit_status);
#endif

    shmctl(shm_id, IPC_RMID, NULL);
}
