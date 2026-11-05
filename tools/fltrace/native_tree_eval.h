#ifndef __NATIVE_TREE_EVAL_H__
#define __NATIVE_TREE_EVAL_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <math.h>
#endif

#if !defined(NATIVE_TREE_FORMAT) || NATIVE_TREE_FORMAT != 3
#error "native_tree_model_*.h is from an older generator - regenerate it with gen_single_tree_code.py"
#endif

/*
 * PySTreeD trees in "box" form (see pystreed_to_c.py's tree_to_boxes()):
 * each PC's tree is the list of its positive leaves, each leaf the
 * conjunction of the conditions on its path. A candidate is predicted
 * positive iff it lies in some box. Conditions on fault-level features (PC,
 * Prev_PC as exact address sets, Prev_delta) are the same for every
 * candidate of a fault, so model->fault() resolves them once per fault into
 * a mask of the boxes that can still match; per candidate only the
 * candidate-level ranges (Offset, Offset_from_faulting, Delta) are checked.
 */

/* lo < v <= hi, the same open/closed sides as the tree's v <= threshold splits */
static inline int st_in(st_val_t v, st_val_t lo, st_val_t hi)
{
    return v > lo && v <= hi;
}

static inline int st_box_has(const st_box_t *b, st_val_t offset, st_val_t ofl, st_val_t delta)
{
    return st_in(offset, b->off_lo, b->off_hi) && st_in(ofl, b->ofl_lo, b->ofl_hi) &&
           st_in(delta, b->d_lo, b->d_hi);
}

/* a candidate against the boxes active for its fault (mask from model->fault()) */
static inline int st_predict(const st_model_t *m, uint32_t active, st_val_t offset, st_val_t ofl,
                             st_val_t delta)
{
    for (int b = 0; b < m->nboxes; b++)
        if ((active >> b) & 1u && st_box_has(&m->boxes[b], offset, ofl, delta))
            return 1;
    return 0;
}

#ifndef __KERNEL__
/* slots lo..hi (inclusive) whose index i satisfies lo_open < i <= hi */
static inline void st_slot_range(double lo_open, double hi, int *lo, int *hi_out)
{
    double l = floor((double)lo_open) + 1, h = floor((double)hi);
    *lo = l < 0 ? 0 : (l > 512 ? 512 : (int)l);
    *hi_out = h > 511 ? 511 : (h < -1 ? -1 : (int)h);
}

/*
 * The pointer stage's scan of one faulted page (512 8-byte slots), for the
 * boxes active on this fault: turn their Offset and Offset_from_faulting
 * ranges into the set of slots that could possibly be positive, read only
 * those, and test each nonzero one with its Delta (pointed-to page minus
 * faulting page, in whole pages). Writes each positive's pointed-to page to
 * ptr_pages (at most 512), returns how many; *nread counts slots read.
 * Gives exactly the positives st_predict() would give on all 512 slots.
 */
static inline int st_pointer_scan(const st_model_t *m, uint32_t active, int faulting_location,
                                  const uint64_t *page_data, uint64_t page_addr,
                                  uint64_t page_size, uint64_t *ptr_pages, int *nread)
{
    uint64_t slots[8] = {0};    /* 512-bit mask of slots worth reading */
    int n = 0;

    *nread = 0;
    for (int b = 0; b < m->nboxes; b++) {
        int lo1, hi1, lo2, hi2;
        if (!((active >> b) & 1u))
            continue;
        /* in double: with integer bounds, an unbounded side is the int64
         * extreme, and adding the faulting slot to it would overflow */
        st_slot_range((double)m->boxes[b].off_lo, (double)m->boxes[b].off_hi, &lo1, &hi1);
        st_slot_range((double)m->boxes[b].ofl_lo + faulting_location,
                      (double)m->boxes[b].ofl_hi + faulting_location, &lo2, &hi2);
        int lo = lo1 > lo2 ? lo1 : lo2, hi = hi1 < hi2 ? hi1 : hi2;
        for (int i = lo; i <= hi; i++)
            slots[i >> 6] |= 1ULL << (i & 63);
    }
    for (int w = 0; w < 8; w++) {
        uint64_t bits = slots[w];
        while (bits) {
            int i = (w << 6) + __builtin_ctzll(bits);
            bits &= bits - 1;
            uint64_t v = page_data[i];
            (*nread)++;
            if (v == 0)
                continue;
            uint64_t ptr_page = v & ~(page_size - 1);
            double delta = (double)(((int64_t)ptr_page - (int64_t)page_addr) / (int64_t)page_size);
            if (st_predict(m, active, (double)i, (double)(i - faulting_location), delta))
                ptr_pages[n++] = ptr_page;
        }
    }
    return n;
}
#endif /* !__KERNEL__ */

#endif // __NATIVE_TREE_EVAL_H__
