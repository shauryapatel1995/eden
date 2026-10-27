#ifndef __PREFETCH_H__
#define __PREFETCH_H__

/* Data structures */
typedef struct {
    /* pc/prev_pc are real ~47-bit userspace addresses (confirmed: e.g.
     * 0x7ffff579288d) - uint32_t would silently truncate to the low 32
     * bits, a completely different number than what training saw (PC was
     * read as a full hex string -> int -> float64, never truncated). Must
     * stay wide enough to hold the real value un-truncated; the PySTreeD
     * trees compare PC/prev_pc as exact 64-bit addresses (a float can only
     * tell 47-bit addresses apart 8MB at a time). delta/prev_delta are whole
     * page counts (training's Delta/4096 and Prev_delta/4096, truncated
     * toward zero), computed with integer division and kept as double so
     * they stay exact - a float of the byte difference rounds beyond 16MB. */
    uint64_t pc;
    int offset;
    double delta;
    int offset_from_faulting;
    uint64_t prev_pc;
    double prev_delta;
} FeatureVector;

typedef struct {
    int cache_hit;
    int cache_miss;
    int misprefetch;
    double precision;
    double recall;
    double accuracy;
} PredictionMetrics;

/* methods */
void init_prefetcher();
unsigned long page_prefetch_preds(FeatureVector features[], int *response_arr, int batch_size);
unsigned long page_postfetch_preds(FeatureVector features[], int *response_arr, int batch_size);
unsigned long page_postfetch(fault_t *f, FeatureVector features[], int *response_arr, int chan_id, int *nevicts_needed);
void page_prefetch_spatial(fault_t *f, int chan_id);
void prefetch_book_pressure(fault_t *f, int nprefetches);
/* whether a fault at this PC can produce any spatial/pointer prediction -
 * always true unless a per-PC predictor backend (PySTreeD) overrides it.
 * Returns int, not bool: eden's base/types.h makes bool an int while the
 * PySTreeD files see stdbool's 1-byte _Bool, and a _Bool return read as an
 * int picks up garbage in the upper bytes of %eax */
int prefetch_pc_has_model_spatial(uint64_t pc);
int prefetch_pc_has_model_pointer(uint64_t pc);
#ifdef DO_PREFETCH_SINGLE_TREE
/* PySTreeD fast paths - see tools/fltrace/single_tree_predict_*.c */
int single_tree_spatial_mask(uint64_t pc, uint64_t prev_pc, double prev_delta,
                             int faulting_location);
int single_tree_pointer_candidates(uint64_t pc, uint64_t prev_pc, double prev_delta,
                                   int faulting_location, const uint64_t *page_data,
                                   uint64_t page_addr, uint64_t *ptr_pages, int *nread);
#endif
/* Leap baseline (DO_PREFETCH_LEAP) - see rmem/prefetch_leap.c */
void page_prefetch_leap(fault_t *f, int chan_id);

#endif // __PREFETCH_H
