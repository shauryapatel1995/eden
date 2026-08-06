#ifndef __PREFETCH_H__
#define __PREFETCH_H__

/* Data structures */
typedef struct {
    /* pc/prev_pc are real ~47-bit userspace addresses (confirmed: e.g.
     * 0x7ffff579288d) - uint32_t would silently truncate to the low 32
     * bits, a completely different number than what training saw (PC was
     * read as a full hex string -> int -> float64, never truncated). Must
     * stay wide enough to hold the real value un-truncated; the eventual
     * (float) cast at inference time only rounds to float32 precision,
     * which is a pre-existing, already-validated approximation shared by
     * every other feature on the native-tree path - not the same thing as
     * losing the high bits outright. */
    uint64_t pc;
    int offset;
    float delta;
    int offset_from_faulting;
    uint64_t prev_pc;
    float prev_delta;
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

#endif // __PREFETCH_H
