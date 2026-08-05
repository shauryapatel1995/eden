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
#ifdef benchmark_model
    struct timeval t1, t2;
    double elapsed_time;
#endif
    void *bkend_buf;
    pgflags_t oldflags;
    int nprefetches = 0, noverflow = 0;
    unsigned long long pressure;
    uint64_t *ptr = (uint64_t *) f->page;
    int faulting_location = (f->faulting_addr - f->page) / sizeof(uint64_t);

    assert(ptr);
    /* Setup pointer features */
    for(int i = 0; i < 512; i++, ptr++) {
        FeatureVector *feature = &features[i];
        feature->pc = f->pc;
        feature->offset = i;
        feature->delta = (*ptr - f->page) / 4096;
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
#ifdef benchmark_model
    gettimeofday(&t1, NULL);
#endif
    /* This part of the code runs inference on all 600
     * candidates. For now we are changing the strategy to
     * run if the ptr is actually present.
     *  page_postfetch_preds(features, responses);
     */
#ifdef benchmark_model
    gettimeofday(&t2, NULL);
    elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000000;      // sec to ms
    elapsed_time += (t2.tv_usec - t1.tv_usec);   // us to ms
    printf("%f us.\n", elapsed_time);
#endif
    /*
     * 1. Generate address value.
     * 2. Do the same checks as the ones in readahead plus walking
     * the page table.
     * If is_page_prefetchable succeeds, the page is locked.
     * Unlocking needs to be managed by this function.
     * 3. Run inference on page.
     * 4. Post an async read for the candidate - post_read_prefetch() only
     * guarantees the read was posted, not that it's done (same contract as
     * post_read(), see backend.h). Completion - copying the data in,
     * marking the page present, freeing the backend buf, and clearing
     * PFLAG_WORK_ONGOING - happens later out of the backend's
     * check_for_completions(), not inline here.
     */
    for (int i = 0; i < 512; i++) {
        assert(f);
        uint64_t ptr_val = *((uint64_t *) f->page + i);
        ptr_val = ptr_val & ~CHUNK_MASK;
        if (is_page_prefetchable(f, ptr_val)) {
            /* Do the inference to get prediction */
            page_postfetch_preds(&features[i], responses, 1);
            if (responses[0] == 1 && ptr_val != 0) {
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
                    goto out;
                }
                nprefetches++;
            } else {
                clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
            }
        }
    }

    for (int i = 512; i < 516; i++) {
        uint64_t ptr_val =  f->page + (i - 511);
        ptr_val = ptr_val & ~CHUNK_MASK;
        if(is_page_prefetchable(f, ptr_val)) {
            page_postfetch_preds(&features[i], responses, 1);
            if (responses[0] == 1) {
                bkend_buf = bkend_buf_alloc();
                if (!bkend_buf) {
                    clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
                    continue;
                }
                if (rmbackend->post_read_prefetch(chan_id, f, ptr_val, bkend_buf)) {
                    bkend_buf_free(bkend_buf);
                    clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
                    goto out;
                }
                nprefetches++;
            } else {
                clear_page_flags(f->mr, ptr_val, PFLAG_WORK_ONGOING, &oldflags);
            }
        }
    }

out:
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
