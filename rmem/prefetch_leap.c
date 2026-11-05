/*
 * prefetch_leap.c - Leap prefetcher baseline (Maruf & Chowdhury, ATC'20)
 *
 * Address-only stride prefetcher, ported from DiLOS's Leap-like "majority"
 * prefetcher (dilos-artifact/dilos/core-ddc/prefetcher/majority.cc) so the
 * learned prefetchers can be compared against it on the same system. Opt-in
 * via `make fltrace.so DO_PREFETCH_LEAP=1`; mutually exclusive with
 * DO_PREFETCH (both prefetch from the same point in handle_page_fault()).
 *
 * On each real fault:
 *   1. Infer hits: if this fault lands on the previous batch's stride line
 *      no more than one stride past its last posted page, the app walked
 *      along the stride, so the batch pages it passed without faulting
 *      were in memory when it needed them. Any other fault (a jump off the
 *      line, or far past the batch) counts zero hits: a prefetched page
 *      merely lying between two far-apart faults says nothing about whether
 *      it was used, and counting it inflates the window. Eden maps
 *      prefetched pages straight into the page table, so a real access to
 *      one is invisible to us (no fault, and no accessed-bit tracking without
 *      a kernel patch); this inference stands in for Leap's swap-cache hit
 *      count. Hit pages are pushed into the history ahead of the faulting
 *      page, as DiLOS does with its hit list.
 *   2. Push this fault's page and its delta into the history.
 *   3. Window = hits + 2, rounded up to a power of two in
 *      [LEAP_MIN_WINDOW, LEAP_MAX_WINDOW], never shrinking below half the
 *      previous window (DiLOS's swapin_nr_pages()).
 *   4. Find the majority delta with Boyer-Moore over the most recent 2, 4,
 *      then 8 history entries. With a trend d, prefetch page + k*d for
 *      k = 1..window. Without one, read ahead window pages in the direction
 *      of the last fault (DiLOS's fallback; original Leap used swap-slot
 *      readahead here).
 *
 * Differences from DiLOS, both deliberate:
 *   - We prefetch `window` pages beyond the faulting page (so never fewer
 *     than 4). DiLOS counts the faulting page as part of the window and so
 *     issues window-1 pages with a trend and window-2 without one.
 *   - With a trend, DiLOS starts at page+1 and then steps by the trend
 *     (page+1, page+1+d, ...); we use page+d, page+2d, ..., which is the
 *     stride the trend describes. The two agree for d = 1.
 *
 * History and hit state are per handler thread, like DiLOS's thread_local
 * state.
 */

#ifdef DO_PREFETCH_LEAP

#include <stdbool.h>
#include <stdint.h>

#include "rmem/backend.h"
#include "rmem/common.h"
#include "rmem/fault.h"
#include "rmem/page.h"
#include "rmem/prefetch.h"
#include "rmem/stats.h"

#define LEAP_HISTORY_SIZE   8   /* DiLOS max_pages */
#define LEAP_MIN_WINDOW     4
#define LEAP_MAX_WINDOW     8
#define LEAP_MIN_REGION     (LEAP_HISTORY_SIZE / 4)

struct leap_entry {
    long delta;             /* in pages, from the previous history entry */
    unsigned long pgoff;    /* page number (addr / CHUNK_SIZE) */
};

/* ring buffer of the most recent accesses; leap_hist_at(0) is the newest */
static __thread struct leap_entry leap_history[LEAP_HISTORY_SIZE];
static __thread int leap_hist_len;
static __thread int leap_hist_next;

/* previous fault and its batch, for hit inference: the batch prefetched
 * prev + k*stride for the k values in leap_last_batch_k (only the ones
 * actually posted, in increasing order) */
static __thread bool leap_have_prev;
static __thread unsigned long leap_prev_pgoff;
static __thread long leap_last_stride;
static __thread int leap_last_batch_k[LEAP_MAX_WINDOW];
static __thread int leap_last_batch_len;
static __thread int leap_last_window;

static inline struct leap_entry *leap_hist_at(int age)
{
    int idx = leap_hist_next - 1 - age;
    if (idx < 0)
        idx += LEAP_HISTORY_SIZE;
    return &leap_history[idx];
}

static void leap_hist_push(unsigned long pgoff)
{
    long delta = leap_hist_len ? (long)(pgoff - leap_hist_at(0)->pgoff) : 0;

    leap_history[leap_hist_next].delta = delta;
    leap_history[leap_hist_next].pgoff = pgoff;
    leap_hist_next = (leap_hist_next + 1) % LEAP_HISTORY_SIZE;
    if (leap_hist_len < LEAP_HISTORY_SIZE)
        leap_hist_len++;
}

/* Boyer-Moore majority vote over the `size` newest history entries */
static bool leap_find_trend_in_region(int size, long *major_delta)
{
    int maj = 0, count = 1, i;
    long candidate;

    for (i = 1; i < size; i++) {
        if (leap_hist_at(maj)->delta == leap_hist_at(i)->delta)
            count++;
        else
            count--;
        if (count == 0) {
            maj = i;
            count = 1;
        }
    }

    candidate = leap_hist_at(maj)->delta;
    for (i = 0, count = 0; i < size; i++)
        if (leap_hist_at(i)->delta == candidate)
            count++;

    *major_delta = candidate;
    return count > size / 2;
}

/* widen the window 2 -> 4 -> 8 entries until a majority shows up */
static bool leap_find_trend(long *major_delta)
{
    int size;

    for (size = LEAP_MIN_REGION; size <= leap_hist_len; size *= 2)
        if (leap_find_trend_in_region(size, major_delta))
            return true;
    return false;
}

/* DiLOS's swapin_nr_pages(): grow with hits, shrink at most by half */
static int leap_window(int hits)
{
    int pages = hits + 2;
    int roundup = LEAP_MIN_WINDOW;

    while (roundup < pages)
        roundup <<= 1;
    pages = roundup;
    if (pages > LEAP_MAX_WINDOW)
        pages = LEAP_MAX_WINDOW;
    if (pages < leap_last_window / 2)
        pages = leap_last_window / 2;
    leap_last_window = pages;
    return pages;
}

/* count (and record in the history) last-batch pages the app walked past
 * without faulting on them - see the file comment */
static int leap_infer_hits(unsigned long pgoff)
{
    long dist, m;
    int i, hits = 0;

    if (!leap_have_prev || leap_last_batch_len == 0)
        return 0;

    /* this fault must sit at prev + m*stride, 1 <= m <= last k + 1 */
    dist = (long)(pgoff - leap_prev_pgoff);
    if (dist % leap_last_stride != 0)
        return 0;
    m = dist / leap_last_stride;
    if (m < 1 || m > leap_last_batch_k[leap_last_batch_len - 1] + 1)
        return 0;

    for (i = 0; i < leap_last_batch_len && leap_last_batch_k[i] < m; i++) {
        leap_hist_push(leap_prev_pgoff + leap_last_batch_k[i] * leap_last_stride);
        hits++;
    }
    return hits;
}

/* Called from handle_page_fault() right after the faulting page's read is
 * posted, like page_prefetch_spatial() - Leap only needs the address. */
void page_prefetch_leap(fault_t *f, int chan_id)
{
    unsigned long pgoff = f->page / CHUNK_SIZE;
    unsigned long candidate;
    pgflags_t oldflags;
    void *bkend_buf;
    long stride;
    int hits, window, k, nprefetches = 0;

    unsigned long start_tsc = rdtsc();
    hits = leap_infer_hits(pgoff);
    RSTAT(LEAP_HITS) += hits;
    leap_hist_push(pgoff);
    window = leap_window(hits);

    if (leap_find_trend(&stride) && stride != 0) {
        RSTAT(LEAP_TREND_FOUND)++;
    } else {
        RSTAT(LEAP_NO_TREND)++;
        stride = (leap_have_prev && pgoff < leap_prev_pgoff) ? -1 : 1;
    }

    unsigned long act_tsc = rdtsc();
    RSTAT(LEAP_DECIDE_CYCLES) += act_tsc - start_tsc;

    leap_last_batch_len = 0;
    for (k = 1; k <= window; k++) {
        candidate = (pgoff + k * stride) * CHUNK_SIZE;
        if (!is_page_prefetchable(f, candidate))
            continue;

        /* best-effort, same as prefetch_act(): skip on no buffer, stop on
         * a busy backend (the rest would almost certainly be busy too) */
        bkend_buf = bkend_buf_alloc();
        if (!bkend_buf) {
            clear_page_flags(f->mr, candidate, PFLAG_WORK_ONGOING, &oldflags);
            continue;
        }
        if (rmbackend->post_read_prefetch(chan_id, f, candidate, bkend_buf)) {
            bkend_buf_free(bkend_buf);
            clear_page_flags(f->mr, candidate, PFLAG_WORK_ONGOING, &oldflags);
            RSTAT(PREFETCH_BACKEND_BUSY)++;
            break;
        }
        leap_last_batch_k[leap_last_batch_len++] = k;
        nprefetches++;
    }

    RSTAT(LEAP_PREFETCHES) += nprefetches;
    prefetch_book_pressure(f, nprefetches);
    RSTAT(LEAP_ACT_CYCLES) += rdtsc() - act_tsc;

    leap_last_stride = stride;
    leap_prev_pgoff = pgoff;
    leap_have_prev = true;
}

#endif /* DO_PREFETCH_LEAP */
