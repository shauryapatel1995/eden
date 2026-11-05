/*
 * single_tree_predict_pointer.c - hand-compiled tree evaluator for the
 * pointer-chase (Type=1) prefetch model, generated per-PC from a set of
 * trained PySTreeD models (see gen_single_tree_code.py /
 * native_tree_model_pointer.h). Each relevant PC gets its own small tree;
 * get_native_nodes() (defined in the generated header) routes a
 * candidate's PC to its tree's node array, or returns NULL for a PC with
 * no trained tree, in which case the candidate is treated as
 * "not prefetchable" without walking anything.
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

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "rmem/common.h"
#include "rmem/prefetch.h"
#include "native_tree_model_pointer.h"
#include "native_tree_eval.h"

/*
 * Whether a fault at this PC could ever get a positive pointer prediction:
 * false if the PC has no trained pointer tree, or its tree has no positive
 * leaf. Checked once per fault (see prefetch.c). Overrides the always-true
 * weak default in rmem/prefetch.c.
 */
int prefetch_pc_has_model_pointer(uint64_t pc)
{
    st_model_t m;
    return st_pointer_lookup(pc, &m);
}

/*
 * The pointer stage for one fault, prediction first: resolve the fault-level
 * conditions once (model->fault()), then scan only the slots the active
 * boxes allow (st_pointer_scan() in native_tree_eval.h). Features match
 * training's collect_pointers(): Offset = slot index, Offset_from_faulting =
 * slot - faulting slot, Delta = (pointed-to page - faulting page) in pages.
 */
int single_tree_pointer_candidates(uint64_t pc, uint64_t prev_pc, double prev_delta,
                                   int faulting_location, const uint64_t *page_data,
                                   uint64_t page_addr, uint64_t *ptr_pages, int *nread)
{
    st_model_t m;

    *nread = 0;
    if (!st_pointer_lookup(pc, &m))
        return 0;
    uint32_t active = m.fault(pc, prev_pc, prev_delta);
    if (!active)
        return 0;
    return st_pointer_scan(&m, active, faulting_location, page_data, page_addr, CHUNK_SIZE,
                           ptr_pages, nread);
}

/*
 * Batched prediction (the generic page_*_preds() entry point, see
 * xgboost_prefetcher.c). Every candidate of a batch comes from the same fault,
 * but this doesn't assume it: the model and active-box mask are recomputed
 * whenever the PC or fault-level features change.
 */
int native_predict_batch_pointer(FeatureVector *features_batch, int batch_size,
                          int *response_arr) {
    st_model_t m;
    int have = 0;
    uint64_t pc = 0, prev_pc = 0;
    double prev_delta = 0;
    uint32_t active = 0;
    for (int i = 0; i < batch_size; i++) {
        FeatureVector *fv = &features_batch[i];
        if (i == 0 || fv->pc != pc || fv->prev_pc != prev_pc || fv->prev_delta != prev_delta) {
            pc = fv->pc; prev_pc = fv->prev_pc; prev_delta = fv->prev_delta;
            have = st_pointer_lookup(pc, &m);
            active = have ? m.fault(pc, prev_pc, prev_delta) : 0;
        }
        response_arr[i] = active ? st_predict(&m, active, fv->offset, fv->offset_from_faulting,
                                              fv->delta) : 0;
    }
    return 0;
}

#endif // DO_PREFETCH
