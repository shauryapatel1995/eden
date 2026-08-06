/*
 * native_prefetch_predict.c - hand-compiled tree evaluator for the
 * prefetch model, generated from a trained XGBoost model via
 * gen_tree_code.py (see native_tree_model.h). Bypasses libxgboost.so
 * entirely for inference - no DMatrix, no JSON config parsing, no OpenMP
 * thread-pool dispatch, just array-indexed tree traversal.
 *
 * Only usable because the model is retrained/regenerated at build time
 * (native_tree_model.h is baked into the binary) - unlike the libxgboost
 * path, this can't load a different model file at runtime.
 */

#ifdef DO_PREFETCH

#include <stdint.h>
#include <math.h>
#include "rmem/common.h"
#include "rmem/prefetch.h"
#include "native_tree_model.h"

#define NATIVE_FEATURE_PC 0
#define NATIVE_FEATURE_OFFSET 1
#define NATIVE_FEATURE_DELTA 2
#define NATIVE_FEATURE_OFFSET_FROM_FAULTING 3
#define NUM_FEATURES_NATIVE 4

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
int native_predict_batch(FeatureVector *features_batch, int batch_size,
                          int *response_arr) {
    for (int i = 0; i < batch_size; i++) {
        float feat[NUM_FEATURES_NATIVE];
        feat[NATIVE_FEATURE_PC] = (float)features_batch[i].pc;
        feat[NATIVE_FEATURE_OFFSET] = (float)features_batch[i].offset;
        feat[NATIVE_FEATURE_DELTA] = features_batch[i].delta / 4096.0f;
        feat[NATIVE_FEATURE_OFFSET_FROM_FAULTING] = (float)features_batch[i].offset_from_faulting;

        response_arr[i] = (native_predict_margin(feat) >= 0.0f) ? 1 : 0;
    }
    return 0;
}

#endif // DO_PREFETCH
