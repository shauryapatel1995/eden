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

/*
 * Implementation to do the actual prefetching with predictions from a
 * prefetching policy. The function does the following -
 * 1. Constructs the required input for a prefetching policy.
 * 2. Obtains predictions from a given policy.
 * 3. Converts predictions to addresses to prefetch
 * 4. Checks whether the page for an address can be prefetched.
 * 5. Prefetches the page, and updates the eviction count to reflect
 * the additional pages.
*/
unsigned long page_postfetch(fault_t * f, FeatureVector *features,
                                int *responses, int chan_id, int *nevicts_needed)
{
    void *bkend_buf;
    pgflags_t oldflags;
    int nprefetches = 0, noverflow = 0;
    unsigned long long pressure;
    int faulting_location = (f->faulting_addr - f->page) / sizeof(uint64_t);
    bool stop_early;

    /* candidates that pass is_page_prefetchable() (and are thus locked -
     * either a prefetch gets posted for them or their lock must be
     * released) get compacted here, so the batched inference call below
     * only scores candidates we could actually act on instead of all 516
     * regardless of prefetchability - most candidates don't pass (already
     * present, unregistered, etc.), so scoring all of them unconditionally
     * made XGBoost evaluate far more rows per fault than before, not
     * fewer calls over the same rows */
    FeatureVector compact_features[516];
    int compact_responses[516];
    uint64_t compact_ptr_val[516];
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
    /* Setup next-N features */
    for (int i = 512; i < 600; i++) {
        FeatureVector *feature = &features[i];
        feature->pc = f->pc;
        feature->offset = 0;
        feature->delta = i - 511;
        feature->offset_from_faulting = 0;
        responses[i] = 0;
    }

    /*
     * Pass 1: check is_page_prefetchable() for every candidate (512
     * pointer-chase + 4 sequential) - this both filters out candidates we
     * can't act on anyway (already present, not registered, etc.) and
     * locks (PFLAG_WORK_ONGOING) the ones that pass. Compact only the
     * locked ones so the batched inference call below scores just the
     * candidates we could actually prefetch, instead of wasting cycles on
     * ones we already know we'll skip.
     */
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
    for (int i = 512; i < 516; i++) {
        uint64_t ptr_val = f->page + (i - 511);
        ptr_val = ptr_val & ~CHUNK_MASK;
        if (is_page_prefetchable(f, ptr_val)) {
            compact_features[ncandidates] = features[i];
            compact_ptr_val[ncandidates] = ptr_val;
            ncandidates++;
        }
    }

    /*
     * Pass 2: one batched inference call for every locked candidate,
     * instead of up to ncandidates individual predict calls each paying
     * its own per-call overhead.
     */
    RSTAT(PREFETCH_CANDIDATES_GATED) += ncandidates;
    if (ncandidates > 0)
        page_postfetch_preds(compact_features, compact_responses, ncandidates);
    for (int k = 0; k < ncandidates; k++)
        if (compact_responses[k] == 1)
            RSTAT(PREFETCH_CANDIDATES_POSITIVE)++;

    /*
     * Pass 3: act on results. Every candidate here is already locked, so
     * on backend-busy we can't just stop early like the old single-pass
     * code did (that was safe there only because not-yet-checked
     * candidates hadn't been locked yet) - remaining locked candidates
     * must still have their lock released or they'd leak.
     */
    stop_early = false;
    for (int k = 0; k < ncandidates; k++) {
        uint64_t ptr_val = compact_ptr_val[k];
        if (stop_early) {
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
                stop_early = true;
                continue;
            }
            nprefetches++;
        } else {
            clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
        }
    }

    /*
     * book memory pressure for the prefetch reads we just posted (mirrors
     * how handle_page_fault() accounts pressure right after posting a
     * regular read, before it completes - see fault.c) and figure out if
     * evictions are needed. Runs even after an early exit above so
     * already-posted prefetches from this call are never left unaccounted.
     */
    assert(nevicts_needed);
    if (nprefetches > 0) {
        pressure = atomic64_add_and_fetch(&memory_used, nprefetches * CHUNK_SIZE);
        log_debug("%s - memory pressure from %d posted prefetch(es) %llu, "
            "limit %lu", FSTR(f), nprefetches, pressure, local_memory);
        if (pressure > local_memory) {
            noverflow = (pressure - local_memory) / CHUNK_SIZE;
            *nevicts_needed = (noverflow < nprefetches) ? noverflow : nprefetches;
        }

        /* update maximum memory usage counter. FIXME: should use CAS! */
        if (pressure > atomic64_read(&max_memory_used))
            atomic64_write(&max_memory_used, pressure);

        log_debug("%s - %d prefetch read(s) posted, pressure %llu evicts %d",
            FSTR(f), nprefetches, pressure, *nevicts_needed);
    }
    return 0;
}
