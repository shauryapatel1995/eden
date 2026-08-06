/*
 * handler.h - dedicated handler core for remote memory
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <linux/userfaultfd.h>
#include <stdio.h>
#include <unistd.h>

#include "base/cpu.h"
#include "base/mem.h"
#include "base/sampler.h"
#include "rmem/backend.h"
#include "rmem/common.h"
#include "rmem/config.h"
#include "rmem/dump.h"
#include "rmem/eviction.h"
#include "rmem/fault.h"
#include "rmem/fsampler.h"
#include "rmem/handler.h"
#include "rmem/page.h"
#include "rmem/pgnode.h"
#include "rmem/region.h"
#include "rmem/uffd.h"

#ifndef RMEM_STANDALONE
#include "../runtime/defs.h"
#endif

/* handler state */
__thread struct hthread *my_hthr = NULL;
__thread int current_stealing_kthr_id = -1;
__thread unsigned long current_blocking_page = 0;
__thread bool current_page_unblocked = false;
#ifdef DO_TRACING
int pagefault_index = 0;
#endif
#if defined(DO_TRACING) || defined(DO_PREFETCH)
/* running state for prev_pc/prev_delta, updated once per real fault right
 * where it's created below - single handler thread owns this, same
 * RMEM_STANDALONE reasoning as elsewhere in this file (no cross-thread
 * fault stealing in our build, see handle_page_fault()) */
static unsigned long trace_last_pc = 0;
static unsigned long trace_last_page = 0;
static long trace_last_delta = 0;
static bool trace_have_last = false;   /* false until the first real fault
                                          * has been seen - avoids a bogus
                                          * huge "delta" (this_page - 0) on
                                          * that very first fault */
#endif

/* check if a fault already exists in the wait queue */
bool does_fault_exist_in_wait_q(struct fault *fault)
{
    struct fault *f;
    list_for_each(&my_hthr->fault_wait_q, f, link) {
        if (f->page == fault->page)
            return true;
    }
    return false;
}

/* called after fetched pages are ready on handler read completions */
int hthr_fault_read_done(fault_t* f)
{
    int r, nevicts;
    r = fault_read_done(f);
    assertz(r);

    /* release fault. this path has no natural place to act on
     * nevicts_needed (e.g. from prefetching in fault_done()), so we
     * just assert it doesn't ask for any - eviction is handled by the
     * caller for the fault that led here */
    fault_done(f, my_hthr->bkend_chan_id, &nevicts);
    assert(nevicts < 1);
    return 0;
}

#ifndef RMEM_STANDALONE

/** 
 * Targeted stealing from Shenango kthreads
 */

/* called on the completions stolen from the shenango kthreads */
int hthr_fault_read_steal_done(fault_t* f)
{
    int r, nevicts;
    struct kthread* owner;

    /* get owner kthread */
    assert(current_stealing_kthr_id >= 0);
    owner = allks[current_stealing_kthr_id];
    assert(owner);
    log_debug("%s - stolen by handler", FSTR(f));

    /* check for bugs */
    assert(f);
    assert(my_hthr->bkend_chan_id != f->posted_chan_id);    /* assert steal */
    assert(owner->bkend_chan_id == f->posted_chan_id);      /* assert owner */
    assert(!f->from_kernel);        /* stole it from shenango threads */
    assert(f->bkend_buf);           /* expecting read buffer */
    assert(!f->stolen_from_cq);     /* no double-steal */

    /* mark as stolen */
    f->stolen_from_cq = 1;
    RSTAT(READY_STEALS)++;

    /* finish servicing the fault */
    r = fault_read_done(f);
    assertz(r);

    /* set the thread ready */
    assert(f->thread);
    thread_ready_safe(owner, f->thread);

    /* check if this is the target blocking page */
    if (f->page == current_blocking_page)
        current_page_unblocked = true;

    /* release fault. see hthr_fault_read_done() for why we assert on
     * nevicts here instead of acting on it */
    fault_done(f, my_hthr->bkend_chan_id, &nevicts);
    assert(nevicts < 1);
    return 0;
}

/* try to unblock a kernel/handler fault that has been waiting for a while.
 * currently, we look at the kthread that locked the page and perform 
 * targeted stealing to progress its faults and release the page */
bool handler_try_unblock_fault(fault_t* f)
{
    struct kthread* owner;
    int nfaults_stolen, ntotal;
    pginfo_t pginfo;
    pgthread_t kthr_id;
    pgflags_t pflags;
    bool unblocked;

    /* check if already unlocked */
    pginfo = get_page_info(f->mr, f->page);
    pflags = get_flags_from_pginfo(pginfo);
    if (!(pflags & PFLAG_WORK_ONGOING))
        /* unlocked */
        return true;
    
    /* get the kthread that is working on the faulting page */
    kthr_id = get_thread_from_pginfo(pginfo);
    if (!kthr_id)
        /* then it may have moved on by the time we got here */
        return false;

    owner = allks[kthr_id - 1];
    assert(owner);
    log_debug("%s - found kthread %d blocking the page", FSTR(f), kthr_id-1);

    /* save the owner kthread id globally so it is visible to the completion
     * callbacks. similarly, also save the blocking page so we can figure out 
     * if we really unblocked the target page in the callbacks */
    assert(current_stealing_kthr_id == -1);
    assert(current_blocking_page == 0);
    assert(!current_page_unblocked);
    current_stealing_kthr_id = kthr_id - 1;
    current_blocking_page = f->page;
    store_release(&current_page_unblocked, false);

    /* check completions with handler stealing callbacks; we don't need to 
     * lock the owner thread as completion-stealing is thread-safe */
    ntotal = rmbackend->check_for_completions(owner->bkend_chan_id, 
        &hthr_stealer_cbs, RMEM_MAX_COMP_PER_OP, &nfaults_stolen, NULL);
    log_debug("handler stole %d completions on chan %d, %d of them reads",
            ntotal, owner->bkend_chan_id, nfaults_stolen);

    /* remove the stolen faults from owner kthreads count */
    if (nfaults_stolen) {
        spin_lock(&owner->pf_lock);
        owner->pf_pending -= nfaults_stolen;
        spin_unlock(&owner->pf_lock);
    }

    /* stealing done; reset the globally visible state */
    unblocked = load_acquire(&current_page_unblocked);
    assert(current_stealing_kthr_id == (kthr_id - 1));
    current_stealing_kthr_id = -1;
    current_blocking_page = 0;
    current_page_unblocked = false;

    return unblocked;
}

#endif

/* poll for faults/other notifications coming from UFFD */
static inline fault_t* read_uffd_fault()
{
    ssize_t read_size;
    struct uffd_msg message;
    struct fault* fault;
    unsigned long long addr, flags;
    unsigned long fault_create_tsc;
#if defined(UFFD_PC_SUPPORTED) || defined(DO_TRACING) || defined(DO_PREFETCH)
    unsigned long pc;
#endif
#if defined(DO_TRACING) || defined(DO_PREFETCH)
    unsigned long this_page;
    unsigned long prev_pc_snapshot;
    long prev_delta_snapshot;
#endif
    struct region_t* mr;

    struct pollfd evt = { .fd = userfault_fd, .events = POLLIN };
    if (poll(&evt, 1, 0) > 0) {
        /* we have something pending on ths fd */
        if ((evt.revents & POLLERR) || (evt.revents & POLLHUP)) {
            log_warn_ratelimited("unexpected wrong poll event from uffd");
            return NULL;
        }

        /* read uffd event data into message */
        read_size = read(evt.fd, &message, sizeof(struct uffd_msg));
        if (unlikely(read_size != sizeof(struct uffd_msg))) {
            /* EAGAIN is fine; another handler may have gotten to it first */
            if (errno != EAGAIN) {
                log_err("unexpected read size %ld, errno %d on uffd", 
                    read_size, errno);
                BUG();
            }
            return NULL;
        }

        /* stamp as early as possible - closest we can get to "when did
         * the app thread actually start blocking" without kernel support
         * for the real fault time */
        fault_create_tsc = rdtsc();

        /* only need page fault events */
        if (unlikely(message.event != UFFD_EVENT_PAGEFAULT)) {
            /* we don't need other events right now; a lot of them are 
             * for reporting changes to memory layout to the handler, but 
             * we hope to handle them with memory lib interposition (see 
             * fltrace.c) or provide explicit calls (see rmem_api.c) */
            log_err("uffd event %d not supported", message.event);
            BUG();
        }

        /* new fault */
        addr = message.arg.pagefault.address;
        flags = message.arg.pagefault.flags;
#if defined(UFFD_PC_SUPPORTED) || defined(DO_TRACING) || defined(DO_PREFETCH)
        pc = message.arg.pagefault.pc;
#endif
#if defined(DO_TRACING) || defined(DO_PREFETCH)
        /* snapshot prev state before overwriting it with this fault's own
         * values - prev_pc_snapshot/prev_delta_snapshot are what get handed
         * to this fault (both for tracing and, further below, fault->prev_pc/
         * prev_delta); trace_last_* then get updated to reflect this fault so
         * the *next* fault sees these as its "prev" */
        this_page = addr & ~CHUNK_MASK;
        prev_pc_snapshot = trace_last_pc;
        prev_delta_snapshot = trace_last_delta;
        trace_last_delta = trace_have_last ?
            (long)(this_page - trace_last_page) : 0;
        trace_last_page = this_page;
        trace_last_pc = pc;
        trace_have_last = true;
#endif
#ifdef DO_TRACING
        /* one line per fault for offline prefetcher training-data
         * collection: page address, exact faulting address, pc, pc of the
         * immediately preceding real fault, page-delta between the
         * 2nd-to-last and last real fault, and page-delta between this
         * fault and the last one */
        pagefault_index++;
        fprintf(stderr, "\"%d PF addr, faulting addr, and ip\", %lx %llx %lx %lx %ld %ld\n",
            pagefault_index, this_page, addr, pc,
            prev_pc_snapshot, prev_delta_snapshot, trace_last_delta);
#endif
        log_debug("uffd pagefault event %d: addr=%llx, flags=0x%llx",
            message.event, addr, flags);

        /* create new fault object */
        fault = fault_alloc();
        if (unlikely(!fault)) {
            log_debug("couldn't get a fault object");
            return NULL;    /* we'll try again later */
        }
    
        /* populate it */
        memset(fault, 0, sizeof(fault_t));
        fault->page = addr & ~CHUNK_MASK;
        fault->faulting_addr = addr;
        fault->create_tsc = fault_create_tsc;
#ifdef UFFD_PC_SUPPORTED
        fault->pc = pc;
#endif
#if defined(DO_TRACING) || defined(DO_PREFETCH)
        fault->prev_pc = prev_pc_snapshot;
        fault->prev_delta = prev_delta_snapshot;
#endif
        fault->is_wrprotect = !!(flags & UFFD_PAGEFAULT_FLAG_WP);
        fault->is_write = !!(flags & UFFD_PAGEFAULT_FLAG_WRITE);
        fault->is_read = !(fault->is_write || fault->is_wrprotect);
        fault->from_kernel = true;
        fault->rdahead_max = 0;   /*no readaheads for kernel faults*/
        fault->rdahead  = 0;
        fault->evict_prio = evict_nprio - 1;

        /* find associated region */
        mr = get_region_by_addr_safe(fault->page);
        BUG_ON(!mr);  /* we dont do region deletions yet so it must exist */
        assert(mr->addr);
        fault->mr = mr;

#ifdef FAULT_SAMPLER
        /* check if this is the first fault on the page; there may be many 
         * concurrent "first" faults to a page but only one of them can be 
         * captured as zero-page fault if we just use PFLAG_REGISTERED. So we
         * use PFLAG_PRESENT_ZERO_PAGED to indicate if a page is currently 
         * exists in local memory before its first-ever eviction and any faults
         * to such locally-present page must be a concurrent zero-page faults */
        pgflags_t pflags = get_page_flags(mr, addr);
        if (!(pflags & PFLAG_REGISTERED) || (pflags & PFLAG_PRESENT_ZERO_PAGED))
            flags |= FSAMPLER_FAULT_FLAG_ZERO;

        /* record if sampling faults */
#ifdef UFFD_PC_SUPPORTED
        /* the pc-supporting kernel patch repurposes uffd_msg.pagefault's
         * feat union for the pc field, so the faulting thread id is no
         * longer available here */
        fsampler_add_fault_sample(my_hthr->fsampler_id, addr, flags, 0);
#else
        fsampler_add_fault_sample(my_hthr->fsampler_id, addr, flags,
            message.arg.pagefault.feat.ptid);
#endif
#endif

        return fault;
    }
    return NULL;
}

/**
 * Main handler thread function
 */
static void* rmem_handler(void *arg) 
{
    /* handler threads run entirely in runtime */
    preempt_disable();
    /* also mark this thread as "runtime" for fltrace.c/stat.c's own
     * malloc/mmap interposition (a separate, RMEM_STANDALONE-specific
     * guard from preempt_disable() above): without this, this thread's own
     * setup allocations (e.g. zero_page_init_thread()'s aligned_alloc())
     * get treated as app memory and routed through jemalloc -> rmalloc(),
     * which can deadlock waiting on this very thread to service the fault
     * that routing creates. Never re-entered with RUNTIME_EXIT() - this
     * thread never runs application code. */
    RUNTIME_ENTER();

    bool need_eviction, work_done;
    unsigned long long pressure;
    fault_t *fault, *next;
    int nevicts, nevicts_needed, batch, r;
    enum fault_status fstatus;
    assert(arg != NULL);        /* expecting a hthread_t */
    my_hthr = (hthread_t*) arg; /* save our hthread_t */
    unsigned long now_tsc, last_tsc;

    /* init per-thread resources */
    r = thread_init_perthread(); assertz(r); /* for tcache support */
    rmem_common_init_thread(&my_hthr->bkend_chan_id, my_hthr->rstats, 0);
    list_head_init(&my_hthr->fault_wait_q);
    my_hthr->n_wait_q = 0;
#ifdef FAULT_SAMPLER
    my_hthr->fsampler_id = fsampler_get_sampler();
#endif

    /* do work */
    last_tsc = 0;
    while(!my_hthr->stop)
    {
        /* account time spent in last iteration */
        now_tsc = rdtsc();
        if (last_tsc) {
            RSTAT(TOTAL_CYCLES) += now_tsc - last_tsc;
            if (work_done)
                RSTAT(WORK_CYCLES) += now_tsc - last_tsc;
        }
        last_tsc = now_tsc;

        /* reset every iteration */
        need_eviction = false;
        work_done = false;
        nevicts = nevicts_needed = 0;

#ifdef RMEM_STANDALONE
        /* check for page nodes (from the pages unmapped on the 
         * application threads) that are waiting to-be-freed */
        rmpage_node_tbf_try_release();
#endif

        /* pick faults from the backlog (wait queue) first */
        fault = list_top(&my_hthr->fault_wait_q, fault_t, link);
        while (fault != NULL) {
            next = list_next(&my_hthr->fault_wait_q, fault, link);
            {
                unsigned long __hpf_tsc = rdtsc();
                fstatus = handle_page_fault(my_hthr->bkend_chan_id, fault,
                    &nevicts_needed, &hthr_cbs);
                RSTAT(HANDLE_FAULT_CYCLES) += rdtsc() - __hpf_tsc;
            }
            switch (fstatus) {
                case FAULT_DONE:
                    log_debug("%s - done, released from wait", FSTR(fault));
                    list_del_from(&my_hthr->fault_wait_q, &fault->link);
                    assert(my_hthr->n_wait_q > 0);
                    my_hthr->n_wait_q--;
                    fault_done(fault, my_hthr->bkend_chan_id, &nevicts_needed);
                    work_done = true;
                    break;
                case FAULT_READ_POSTED:
                    log_debug("%s - done, released from wait", FSTR(fault));
                    list_del_from(&my_hthr->fault_wait_q, &fault->link);
                    assert(my_hthr->n_wait_q > 0);
                    my_hthr->n_wait_q--;
                    work_done = true;
                    if (nevicts_needed > 0)
                        goto eviction;
                    break;
                case FAULT_IN_PROGRESS:
                    log_debug("%s - not released from wait", FSTR(fault));
                    RSTAT(WAIT_RETRIES)++;

#ifndef RMEM_STANDALONE
                    /* if the fault has been waiting too long, try unblocking */
                    if (unlikely(fault->tstamp_tsc && 
                        (now_tsc - fault->tstamp_tsc) > 
                            HANDLER_WAIT_BEFORE_STEAL_US * cycles_per_us))
                    {
                        log_warn_ratelimited("%s - waited too long", FSTR(fault));
                        fault->tstamp_tsc = now_tsc;
                        if (handler_try_unblock_fault(fault))
                            /* if unblocked, try this fault again */
                            continue;
                    }
#endif
                    break;
            }

            /* go to next fault */
            fault = next;
        }

        /* check for incoming uffd faults */
        fault = read_uffd_fault();
        if (fault) {
            /* accounting */
            RSTAT(FAULTS)++;
            if (fault->is_read)         RSTAT(FAULTS_R)++;
            if (fault->is_write)        RSTAT(FAULTS_W)++;
            if (fault->is_wrprotect)    RSTAT(FAULTS_WP)++;
            if (fault->evict_prio == 0) RSTAT(FAULTS_P0)++;
            work_done = true;

            /* start handling fault */
            {
                unsigned long __hpf_tsc = rdtsc();
                fstatus = handle_page_fault(my_hthr->bkend_chan_id, fault,
                    &nevicts_needed, &hthr_cbs);
                RSTAT(HANDLE_FAULT_CYCLES) += rdtsc() - __hpf_tsc;
            }
            switch (fstatus) {
                case FAULT_DONE:
                    fault_done(fault, my_hthr->bkend_chan_id, &nevicts_needed);
                    break;
                case FAULT_IN_PROGRESS:
                    /* handler thread should not see duplicate faults as we 
                     * don't expect kernel to send the same fault twice; 
                     * although duplicate faults seems to occur when debugging 
                     * with GDB after a previously faulting thread is let go 
                     * from a breakpoint, so comment it out when debugging */
                    // assert(!does_fault_exist_in_wait_q(fault));

                    /* add to wait, with a timestamp */
                    assertz(fault->tstamp_tsc);
                    fault->tstamp_tsc = rdtsc();
                    list_add_tail(&my_hthr->fault_wait_q, &fault->link);
                    my_hthr->n_wait_q++;
                    log_debug("%s - added to wait", FSTR(fault));
                    break;
                case FAULT_READ_POSTED:
                    /* nothing to do here, we check for completions later*/
                    break;
            }
        }

eviction:
        /*  do eviction if needed */
        need_eviction = (nevicts_needed > 0);
        if (!need_eviction) {
            /* if eviction wasn't already signaled by the earlier fault, 
             * see if we need one in general (since this is the handler thread)*/
            pressure = atomic64_read(&memory_used);
            need_eviction = (pressure > local_memory * eviction_threshold);
        }

        /* start eviction */
        if (need_eviction) {
            nevicts = 0;
            do {
                /* can use bigger batches in handler threads if idling */
                batch = evict_batch_size;
                if (nevicts_needed > 0) 
                    batch = EVICTION_MAX_BATCH_SIZE;
                nevicts += do_eviction(my_hthr->bkend_chan_id, &hthr_cbs, batch);
            } while(nevicts < nevicts_needed);
            work_done = true;
        }

        /* handle read/write completions from the backend */
        r = rmbackend->check_for_completions(my_hthr->bkend_chan_id, &hthr_cbs, 
            RMEM_MAX_COMP_PER_OP, NULL, NULL);
        if (r > 0)
            work_done = true;

        /* check for remote memory dump */
        if (unlikely(dump_rmem_state_and_exit)) {
            dump_rmem_state();
            unreachable();
        }

        /* check for any sampler dumps */
#ifdef EPOCH_SAMPLER
        sampler_dump_provide_tsc(&epoch_sampler, 32, now_tsc);
#endif
#ifdef FAULT_SAMPLER
        fsampler_dump(my_hthr->fsampler_id);
#endif
    }

    /* destroy state */
    rmem_common_destroy_thread();
    assert(list_empty(&my_hthr->fault_wait_q));
    return NULL;
}

/* create a new fault handler thread */
hthread_t* new_rmem_handler_thread(int pincore_id)
{
    int r;
    hthread_t* hthr = aligned_alloc(CACHE_LINE_SIZE, sizeof(hthread_t));
    assert(hthr);
    memset(hthr, 0, sizeof(hthread_t));

    /* create thread */
    hthr->stop = false;
    hthr->fsampler_id = -1;
    r = pthread_create(&hthr->thread, NULL, rmem_handler, (void*)hthr);
    if (r != 0) {
        /* pthread_create() returns 0 on success or a positive errno-style
         * value on failure - it does NOT return a negative value the way
         * plain syscalls do, and does NOT set the global errno. The old
         * "r < 0" check here could never fire, silently swallowing thread
         * creation failures and returning a handle to a thread that was
         * never actually created - the app thread would then fault
         * normally with no handler ever able to service it, hanging
         * forever in handle_userfault with no visible error. */
        log_err("pthread_create for rmem handler failed: %d", r);
        return NULL;
    }

    /* pin thread */
    if (pincore_id >= 0) {
        r = cpu_pin_thread(hthr->thread, pincore_id);
        assertz(r);
    }

    return hthr;
}

/* stop and deallocate a fault handler thread */
int stop_rmem_handler_thread(hthread_t* hthr)
{
    /* signal and wait for thread to stop */
    assert(!hthr->stop);
    hthr->stop = true;
	pthread_join(hthr->thread, NULL);

    /* deallocate */
    free(hthr);
    return 0;
}

/* handler thread backend read/write completion ops for own cq */
struct bkend_completion_cbs hthr_cbs = {
    .read_completion = hthr_fault_read_done,
    .write_completion = owner_write_back_completed
};

#ifndef RMEM_STANDALONE
/* handler thread backend read/write completion ops when stealing */
struct bkend_completion_cbs hthr_stealer_cbs = {
    .read_completion = hthr_fault_read_steal_done,
    .write_completion = stealer_write_back_completed
};
#endif