/*
 * single_tree_predict_spatial.c - hand-compiled tree evaluator for the
 * spatial (Type=0) prefetch model, generated per-PC from a set of trained
 * PySTreeD models (see gen_single_tree_code.py / native_tree_model_spatial.h).
 * Each relevant PC gets its own small tree; get_native_nodes() (defined in
 * the generated header) routes a candidate's PC to its tree's node array,
 * or returns NULL for a PC with no trained tree, in which case the
 * candidate is treated as "not prefetchable" without walking anything.
 *
 * Spatial (Type=0) candidates use a separate model - see
 * single_tree_predict_spatial.c - since spatial and pointer candidates have very
 * different class-imbalance ratios (~6:1 vs ~1400:1 miss:hit on the mcf
 * train dataset) a single shared model tuned for one ratio 
 * predicts positive on ~none of the other type's candidates.
 *
 * Only usable because the model is retrained/regenerated at build time
 * (native_tree_model_pointer.h is baked into the binary).
 */

#ifdef DO_PREFETCH_SINGLE_TREE

#include <stdint.h>
#include <math.h>
#include "rmem/common.h"
#include "rmem/prefetch.h"
#include "native_tree_model_spatial.h"
#include "native_tree_eval.h"

#define NATIVE_FEATURE_PC 0
#define NATIVE_FEATURE_OFFSET 1
#define NATIVE_FEATURE_DELTA 2
#define NATIVE_FEATURE_OFFSET_FROM_FAULTING 3
#define NATIVE_FEATURE_PREV_PC 4
#define NATIVE_FEATURE_PREV_DELTA 5
#define NUM_FEATURES_NATIVE 6

/*
 * Batched native prediction.
 */
int native_predict_batch_spatial(FeatureVector *features_batch, int batch_size,
                          int *response_arr) {
    for (int i = 0; i < batch_size; i++) {
        int num_nodes;
        const native_tree_node_t *nodes = get_native_nodes(features_batch[i].pc, &num_nodes);
        if (!nodes) {
            /* no trained tree for this PC (see get_native_nodes() in
             * native_tree_model_spatial.h) - not prefetchable */
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

        response_arr[i] = (int)native_eval_tree(nodes, feat);
    }
    return 0;
}

#endif // DO_PREFETCH