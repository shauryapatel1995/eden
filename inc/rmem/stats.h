/*
 * stats.h - remote memory stat counters
 */

#ifndef __RMEM_STATS_H__
#define __RMEM_STATS_H__

/*
 * Remote memory stat counters. 
 * Don't use these enums directly. Instead, use the RSTAT() macro in defs.h
 */
enum {
    /* fault stats */
    RSTAT_FAULTS = 0,
    RSTAT_FAULTS_R,
    RSTAT_FAULTS_W,
    RSTAT_FAULTS_WP,
    RSTAT_FAULTS_ZP,
    RSTAT_FAULTS_P0,
    RSTAT_FAULTS_DONE,
    RSTAT_FAULTS_REDUNDANT,
    RSTAT_WP_UPGRADES,
    RSTAT_UFFD_NOTIF,
    RSTAT_UFFD_RETRIES,
    RSTAT_RDAHEADS,
    RSTAT_RDAHEAD_PAGES,
    RSTAT_PREFETCHES,
    RSTAT_PREFETCH_CANDIDATES_GATED,    /* candidates that passed
                                          * is_page_prefetchable() and
                                          * actually got scored by the
                                          * model - out of up to 516
                                          * possible per fault */
    RSTAT_PREFETCH_CANDIDATES_POSITIVE, /* of those, how many the model
                                          * predicted positive on, before
                                          * checking buffer/backend
                                          * availability - RSTAT_PREFETCHES
                                          * is the subset of these that
                                          * actually got a read posted */

    /* eviction stats */
    RSTAT_EVICTS,
    RSTAT_EVICT_POPPED,
    RSTAT_EVICT_NONE,           /* found no eviction candidates */
    RSTAT_EVICT_SUBOPTIMAL,     /* couldn't fill the entire batch size */
    RSTAT_EVICT_WBACK,
    RSTAT_EVICT_WP_RETRIES,
    RSTAT_EVICT_MADV,
    RSTAT_EVICT_DONE,
    RSTAT_EVICT_PAGES_DONE,
    RSTAT_STAGING_EVICTED,     /* pages evicted straight out of the prefetch
                                 * staging area (the intended, cheap case) */
    RSTAT_STAGING_AGED_OUT,    /* pages that outlived staging's capacity and
                                 * got promoted into the real eviction lists
                                 * without ever being evicted from staging -
                                 * rare in practice since normal eviction
                                 * pressure keeps staging well under
                                 * capacity most of the time */

    /* network read/writes */
    RSTAT_NET_READ,
    RSTAT_NET_WRITE,

    /* work stealing */
    RSTAT_READY_STEALS,
    RSTAT_WAIT_STEALS,
    RSTAT_WAIT_RETRIES,         /* time wasted checking on concurrent faults */

    /* memory accounting */
    RSTAT_MALLOC_SIZE,
    RSTAT_MUNMAP_SIZE,
    RSTAT_MADV_SIZE,

    /* time accounting */
    RSTAT_TOTAL_CYCLES,
    RSTAT_WORK_CYCLES,
    RSTAT_BACKEND_WAIT_CYCLES,  /* time wasted because backend is busy  */
    RSTAT_APP_FAULT_WAIT_CYCLES,    /* total time an app thread spent
                                      * actually blocked on a real
                                      * (from_kernel) page fault, summed
                                      * across all such faults - not the
                                      * handler thread's own busy/idle
                                      * time, the faulting thread's */
    RSTAT_PREFETCH_SCAN_CYCLES, /* time spent inside page_postfetch()
                                  * (candidate scan + XGBoost inference) -
                                  * time the single handler thread is NOT
                                  * available to notice/service the next
                                  * real fault, which APP_FAULT_WAIT_CYCLES
                                  * alone doesn't capture (it only starts
                                  * the clock once the handler reads the
                                  * uffd event, not when the fault truly
                                  * occurred) */
    RSTAT_PREFETCH_GATE_CYCLES,  /* subset of PREFETCH_SCAN_CYCLES: just the
                                   * is_page_prefetchable() gating loop over
                                   * up to 516 candidates (pass 1) */
    RSTAT_PREFETCH_INFER_CYCLES, /* subset of PREFETCH_SCAN_CYCLES: just the
                                   * batched model inference call (pass 2) */
    RSTAT_PREFETCH_ACT_CYCLES,   /* subset of PREFETCH_SCAN_CYCLES: just
                                   * posting reads for positive predictions
                                   * (pass 3) */
    RSTAT_HANDLE_FAULT_CYCLES,  /* total handler-thread time spent inside
                                  * handle_page_fault() across every call
                                  * (fresh faults and wait-queue re-checks
                                  * of in-progress ones), summed - this is
                                  * the direct handler-thread cost of
                                  * servicing page faults, separate from
                                  * APP_FAULT_WAIT_CYCLES (the faulting
                                  * thread's own blocked time) */

    /* rmem hints */
    RSTAT_ANNOT_HITS,

    RSTAT_NR,   /* total number of counters */
};

/**
 * RSTAT - gets an remote memory stat counter
 * (this can be used from both shenango & handler threads but make 
 * sure to initialize the ptr to the thread-local stats)
 */
extern __thread uint64_t* rstats_ptr;
static inline uint64_t* rstats_ptr_safe()
{
   	assert(rstats_ptr);
    return rstats_ptr;
}
#define RSTAT(counter) (rstats_ptr_safe()[RSTAT_ ## counter])

/* stat counter names */
extern const char *rstat_names[];

#endif  // __RMEM_STATS_H__