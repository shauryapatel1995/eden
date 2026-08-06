/*
 * native_predict_pointer.c - hand-compiled tree evaluator for the
 * pointer-chase (Type=1) prefetch model, generated from a trained XGBoost
 * model via gen_tree_code.py (see native_tree_model_pointer.h). Bypasses
 * libxgboost.so entirely for inference - no DMatrix, no JSON config
 * parsing, no OpenMP thread-pool dispatch, just array-indexed tree
 * traversal.
 *
 * Spatial (Type=0) candidates use a separate model - see
 * native_predict_spatial.c - since spatial and pointer candidates have very
 * different class-imbalance ratios (~6:1 vs ~1400:1 miss:hit on the mcf
 * train dataset) and need independently-tuned scale_pos_weight; a single
 * shared model tuned for one ratio predicts positive on ~none of the other
 * type's candidates.
 *
 * Only usable because the model is retrained/regenerated at build time
 * (native_tree_model_pointer.h is baked into the binary) - unlike the
 * libxgboost path, this can't load a different model file at runtime.
 */

#ifdef DO_PREFETCH

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "rmem/common.h"
#include "rmem/prefetch.h"
#include "native_tree_model_pointer.h"

#define NATIVE_FEATURE_PC 0
#define NATIVE_FEATURE_OFFSET 1
#define NATIVE_FEATURE_DELTA 2
#define NATIVE_FEATURE_OFFSET_FROM_FAULTING 3
#define NATIVE_FEATURE_PREV_PC 4
#define NATIVE_FEATURE_PREV_DELTA 5
#define NUM_FEATURES_NATIVE 6

/* binary search over native_relevant_pcs (kept sorted by gen_tree_code.py) -
 * ~4% of pointer candidates at runtime come from a PC the training data
 * never had >=100 real cache hits for (checked empirically against the mcf
 * train trace), so the full tree ensemble's decision for them isn't
 * calibrated on any real signal for that PC - skip the expensive part
 * entirely and just answer "not prefetchable" */
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
int native_predict_batch_pointer(FeatureVector *features_batch, int batch_size,
                          int *response_arr) {
    for (int i = 0; i < batch_size; i++) {
        if (!native_pc_is_relevant(features_batch[i].pc)) {
            response_arr[i] = 0;
            continue;
        }
        float feat[NUM_FEATURES_NATIVE];
        feat[NATIVE_FEATURE_PC] = (float)features_batch[i].pc;
        feat[NATIVE_FEATURE_OFFSET] = (float)features_batch[i].offset;
        /* delta already arrives in page-count units (see page_prefetch_spatial()/
         * page_postfetch() in prefetch.c) matching training's Cand_delta/4096 -
         * do NOT divide again here (a previous version of this file did, a
         * 4096x double-scale that likely explains why the deployed spatial
         * model never predicted positive on any of its candidates) */
        feat[NATIVE_FEATURE_DELTA] = features_batch[i].delta;
        feat[NATIVE_FEATURE_OFFSET_FROM_FAULTING] = (float)features_batch[i].offset_from_faulting;
        feat[NATIVE_FEATURE_PREV_PC] = (float)features_batch[i].prev_pc;
        feat[NATIVE_FEATURE_PREV_DELTA] = features_batch[i].prev_delta;

        response_arr[i] = (native_predict_margin(feat) >= 0.0f) ? 1 : 0;
    }
    return 0;
}

#endif // DO_PREFETCH
