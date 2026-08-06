/*
 * prefetch.c - Prefetcher backend implementation
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>
#include <dlfcn.h>
#include "rmem/common.h"
#include "rmem/prefetch.h"
#include "rmem/page.h"
#include "rmem/fault.h"
#include <sys/time.h>

/* act on gated+scored candidates: post a prefetch read for every positive
 * prediction, releasing the is_page_prefetchable() lock on everything else
 * (or on everything, once backend-busy forces an early stop). Shared by
 * both the early spatial phase and the later pointer phase. Returns the
 * number of reads actually posted. On backend-busy we can't just stop
 * early like a single-pass design could - by this point every candidate is
 * already locked (via is_page_prefetchable()), so remaining ones must
 * still have their lock released or they'd leak; that release is real
 * unlocking work, just not a new backend post attempt. */
static int prefetch_act(fault_t *f, int chan_id, uint64_t *compact_ptr_val,
    int *compact_responses, int ncandidates)
{
    void *bkend_buf;
    pgflags_t oldflags;
    int nprefetches = 0;
    bool stop_early = false;

    for (int k = 0; k < ncandidates; k++) {
        uint64_t ptr_val = compact_ptr_val[k];
        if (stop_early) {
            RSTAT(PREFETCH_SKIPPED_STOP_EARLY)++;
            clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
            continue;
        }
        if (compact_responses[k] == 1 && ptr_val != 0) {
            /* each candidate gets its own buf - freed at its own
             * completion, unlike a fault's f->bkend_buf this isn't
             * shared/reused across candidates since multiple reads can
             * be in flight at once */
            bkend_buf = bkend_buf_alloc();
            if (!bkend_buf) {
                /* no free backend bufs - prefetching is best-effort,
                 * so just skip this candidate rather than block/crash */
                clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
                continue;
            }
            if (rmbackend->post_read_prefetch(chan_id, f, ptr_val, bkend_buf)) {
                /* backend busy; stop trying more candidates for this
                 * fault, it'll likely still be busy for the rest */
                bkend_buf_free(bkend_buf);
                clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
                RSTAT(PREFETCH_BACKEND_BUSY)++;
                stop_early = true;
                continue;
            }
            nprefetches++;
        } else {
            clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
        }
    }
    return nprefetches;
}

/* book memory pressure for prefetch reads just posted (mirrors how
 * handle_page_fault() accounts pressure right after posting a regular
 * read, before it completes). Unlike handle_page_fault()'s own accounting,
 * this deliberately does NOT touch *nevicts_needed - both call sites
 * (early spatial, in handle_page_fault() itself; late pointer, in
 * fault_done()) run before some later pressure check that will already
 * see this contribution via the shared memory_used counter and compute
 * the correctly-combined eviction need from there, so setting it here too
 * would just get overwritten. */
static void prefetch_book_pressure(fault_t *f, int nprefetches)
{
    unsigned long long pressure;

    if (nprefetches <= 0)
        return;
    pressure = atomic64_add_and_fetch(&memory_used, nprefetches * CHUNK_SIZE);
    log_debug("%s - memory pressure from %d posted prefetch(es) %llu, "
        "limit %lu", FSTR(f), nprefetches, pressure, local_memory);
    if (pressure > atomic64_read(&max_memory_used))
        atomic64_write(&max_memory_used, pressure);
}

/* Early phase: score and dispatch the 4 spatial/next-N candidates right
 * after the faulted page's own read is posted, instead of waiting for it
 * to complete. Unlike pointer-chase candidates, these need no page content
 * at all - just the faulting address - so there's no reason to make them
 * wait behind the RTT of the read they're piggybacking on. Called from
 * handle_page_fault() in fault.c; see PREFETCH_SPATIAL_*_CYCLES for how
 * much latency this actually moves off the per-fault critical path
 * compared to the pointer-chase path in page_postfetch() below. */
void page_prefetch_spatial(fault_t *f, int chan_id)
{
    FeatureVector features[4];
    int responses[4];
    uint64_t ptr_val[4];
    int ncandidates = 0;
    unsigned long pass_start_tsc;

    unsigned long spatial_start_tsc = rdtsc();

    /* Setup next-N features - pure address arithmetic, no page content */
    for (int i = 0; i < 4; i++) {
        features[i].pc = f->pc;
        features[i].offset = 0;
        features[i].delta = i + 1;
        features[i].offset_from_faulting = 0;
    }

    pass_start_tsc = rdtsc();
    for (int i = 0; i < 4; i++) {
        uint64_t candidate = (f->page + (i + 1) * CHUNK_SIZE) & ~CHUNK_MASK;
        if (is_page_prefetchable(f, candidate)) {
            features[ncandidates] = features[i];
            ptr_val[ncandidates] = candidate;
            ncandidates++;
        }
    }
    RSTAT(PREFETCH_SPATIAL_GATE_CYCLES) += rdtsc() - pass_start_tsc;

    pass_start_tsc = rdtsc();
    RSTAT(PREFETCH_SPATIAL_CANDIDATES_GATED) += ncandidates;
    if (ncandidates > 0)
        page_postfetch_preds(features, responses, ncandidates);
    for (int k = 0; k < ncandidates; k++)
        if (responses[k] == 1)
            RSTAT(PREFETCH_SPATIAL_CANDIDATES_POSITIVE)++;
    RSTAT(PREFETCH_SPATIAL_INFER_CYCLES) += rdtsc() - pass_start_tsc;

    pass_start_tsc = rdtsc();
    int nprefetches = prefetch_act(f, chan_id, ptr_val, responses, ncandidates);
    RSTAT(PREFETCH_SPATIAL_ACT_CYCLES) += rdtsc() - pass_start_tsc;
    RSTAT(PREFETCH_SPATIAL_PREFETCHES) += nprefetches;

    prefetch_book_pressure(f, nprefetches);
    RSTAT(PREFETCH_SPATIAL_SCAN_CYCLES) += rdtsc() - spatial_start_tsc;
}

/*
 * Late phase: score and dispatch the 512 pointer-chase candidates, which
 * genuinely need the faulted page's content (to read real pointer values)
 * and so can only run after it arrives - called from fault_done() once
 * the read completes. The spatial/next-N candidates that used to also run
 * here now run early instead, see page_prefetch_spatial() above.
 */
unsigned long page_postfetch(fault_t * f, FeatureVector *features,
                                int *responses, int chan_id, int *nevicts_needed)
{
    int faulting_location = (f->faulting_addr - f->page) / sizeof(uint64_t);

    /* candidates that pass is_page_prefetchable() (and are thus locked -
     * either a prefetch gets posted for them or their lock must be
     * released) get compacted here, so the batched inference call below
     * only scores candidates we could actually act on instead of all 512
     * regardless of prefetchability - most candidates don't pass (already
     * present, unregistered, etc.), so scoring all of them unconditionally
     * made XGBoost evaluate far more rows per fault than before, not
     * fewer calls over the same rows */
    FeatureVector compact_features[512];
    int compact_responses[512];
    uint64_t compact_ptr_val[512];
    int ncandidates = 0;

    assert(f->page);
    /* Setup pointer features. delta is intentionally NOT computed here
     * (would be (*ptr - f->page) / 4096) - the model was trained with
     * delta zeroed for pointer-type candidates specifically because that
     * was the one feature that required reading the actual pointer value
     * off the faulted page. We tried overlapping inference with the real
     * read using that property (page_prefetch_prescan(), since reverted)
     * but it was a net loss on both LOCAL and RDMA backends: without
     * is_page_prefetchable()'s cheap real-pointer-value gate available
     * before the read completes, prescan had to score all 516 candidates
     * unconditionally (~200us/fault), which is far more than the RDMA
     * round-trip it was meant to hide behind (~10us/fault) - so gating
     * first and only inferring on the (usually much smaller) surviving
     * subset, as below, wins in practice despite still running after the
     * read. */
    for(int i = 0; i < 512; i++) {
        FeatureVector *feature = &features[i];
        feature->pc = f->pc;
        feature->offset = i;
        feature->delta = 0;
        feature->offset_from_faulting = i - faulting_location;
        responses[i] = 0;
    }

    /*
     * Pass 1: check is_page_prefetchable() for every pointer-chase
     * candidate - this both filters out candidates we can't act on anyway
     * (already present, not registered, etc.) and locks (PFLAG_WORK_ONGOING)
     * the ones that pass. Compact only the locked ones so the batched
     * inference call below scores just the candidates we could actually
     * prefetch, instead of wasting cycles on ones we already know we'll
     * skip.
     */
    unsigned long pass_start_tsc = rdtsc();
    for (int i = 0; i < 512; i++) {
        assert(f);
        uint64_t ptr_val = *((uint64_t *) f->page + i);
        ptr_val = ptr_val & ~CHUNK_MASK;
        if (is_page_prefetchable(f, ptr_val)) {
            compact_features[ncandidates] = features[i];
            compact_ptr_val[ncandidates] = ptr_val;
            ncandidates++;
        }
    }
    RSTAT(PREFETCH_GATE_CYCLES) += rdtsc() - pass_start_tsc;

    /*
     * Pass 2: one batched inference call for every locked candidate,
     * instead of up to ncandidates individual predict calls each paying
     * its own per-call overhead.
     */
    pass_start_tsc = rdtsc();
    RSTAT(PREFETCH_CANDIDATES_GATED) += ncandidates;
    if (ncandidates > 0)
        page_postfetch_preds(compact_features, compact_responses, ncandidates);
    for (int k = 0; k < ncandidates; k++)
        if (compact_responses[k] == 1)
            RSTAT(PREFETCH_CANDIDATES_POSITIVE)++;
    RSTAT(PREFETCH_INFER_CYCLES) += rdtsc() - pass_start_tsc;

    /*
     * Pass 3: act on results.
     */
    pass_start_tsc = rdtsc();
    int nprefetches = prefetch_act(f, chan_id, compact_ptr_val, compact_responses, ncandidates);
    RSTAT(PREFETCH_ACT_CYCLES) += rdtsc() - pass_start_tsc;

    /*
     * book memory pressure for the prefetch reads we just posted and
     * figure out if evictions are needed - unlike page_prefetch_spatial(),
     * this DOES set *nevicts_needed since nothing runs after this to fold
     * its contribution in (fault_done() has no further pressure check of
     * its own).
     */
    assert(nevicts_needed);
    prefetch_book_pressure(f, nprefetches);
    if (nprefetches > 0) {
        unsigned long long pressure = atomic64_read(&memory_used);
        if (pressure > local_memory) {
            int noverflow = (pressure - local_memory) / CHUNK_SIZE;
            *nevicts_needed = (noverflow < nprefetches) ? noverflow : nprefetches;
        }
        log_debug("%s - %d prefetch read(s) posted, pressure %llu evicts %d",
            FSTR(f), nprefetches, pressure, *nevicts_needed);
    }
    return 0;
}
