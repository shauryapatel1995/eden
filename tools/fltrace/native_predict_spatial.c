/*
 * native_predict_spatial.c - hand-compiled tree evaluator for the
 * spatial/next-N (Type=0) prefetch model, generated from a trained XGBoost
 * model via gen_tree_code.py (see native_tree_model_spatial.h). Mirrors
 * native_predict_pointer.c's structure exactly - see that file's header
 * comment for why spatial and pointer candidates need separate models
 * (independently-tuned scale_pos_weight for very different class-imbalance
 * ratios) rather than one shared model.
 */

#ifdef DO_PREFETCH

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "rmem/common.h"
#include "rmem/prefetch.h"
#include "native_tree_model_spatial.h"

#define NATIVE_FEATURE_PC 0
#define NATIVE_FEATURE_OFFSET 1
#define NATIVE_FEATURE_DELTA 2
#define NATIVE_FEATURE_OFFSET_FROM_FAULTING 3
#define NATIVE_FEATURE_PREV_PC 4
#define NATIVE_FEATURE_PREV_DELTA 5
#define NUM_FEATURES_NATIVE 6

/* binary search over native_relevant_pcs (kept sorted by gen_tree_code.py) -
 * see native_predict_pointer.c's copy of this comment for the rationale.
 * Spatial's out-of-distribution fraction is much smaller (~1.2% of
 * candidates, checked empirically) than pointer's (~4%), but the check is
 * essentially free either way. */
static inline bool native_pc_is_relevant(uint64_t pc) {
    int lo = 0, hi = NATIVE_NUM_RELEVANT_PCS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (native_relevant_pcs[mid] == pc)
            return true;
        if (native_relevant_pcs[mid] < pc)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return false;
}

static inline float native_eval_tree(int root, const float *feat) {
    int node = root;
    while (native_nodes[node].feature >= 0) {
        float v = feat[native_nodes[node].feature];
        int go_left = isnan(v) ? native_nodes[node].missing_left
                                : (v < native_nodes[node].threshold);
        node = go_left ? native_nodes[node].left : native_nodes[node].right;
    }
    return native_nodes[node].leaf_value;
}

/* raw logit-space margin - matches XGBoosterPredictFromDense's type=1
 * (margin) output to within float rounding, verified against ground truth */
static inline float native_predict_margin(const float *feat) {
    float sum = native_base_score;
    for (int t = 0; t < NATIVE_NUM_TREES; t++)
        sum += native_eval_tree(native_tree_roots[t], feat);
    return sum;
}

/*
 * Batched native prediction - same interface as predict_batch() +
 * probability_to_prediction() combined. Skips the sigmoid/expf() call
 * entirely: since sigmoid is monotonic, sigmoid(margin) >= 0.5 iff
 * margin >= 0, so classification only needs the margin's sign.
 */
int native_predict_batch_spatial(FeatureVector *features_batch, int batch_size,
                          int *response_arr) {
    for (int i = 0; i < batch_size; i++) {
        if (!native_pc_is_relevant(features_batch[i].pc)) {
            response_arr[i] = 0;
            continue;
        }
        float feat[NUM_FEATURES_NATIVE];
        feat[NATIVE_FEATURE_PC] = (float)features_batch[i].pc;
        feat[NATIVE_FEATURE_OFFSET] = (float)features_batch[i].offset;
        /* delta already arrives in page-count units (see page_prefetch_spatial()
         * in prefetch.c) matching training's Cand_delta/4096 - do NOT divide
         * again here */
        feat[NATIVE_FEATURE_DELTA] = features_batch[i].delta;
        feat[NATIVE_FEATURE_OFFSET_FROM_FAULTING] = (float)features_batch[i].offset_from_faulting;
        feat[NATIVE_FEATURE_PREV_PC] = (float)features_batch[i].prev_pc;
        feat[NATIVE_FEATURE_PREV_DELTA] = features_batch[i].prev_delta;

        response_arr[i] = (native_predict_margin(feat) >= 0.0f) ? 1 : 0;
    }
    return 0;
}

#endif // DO_PREFETCH
