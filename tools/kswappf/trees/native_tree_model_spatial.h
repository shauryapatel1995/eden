#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

/* tree format 3: each PC's tree as its positive-leaf boxes - a compiled
 * per-fault function (PC/Prev_PC/Prev_delta -> active boxes) plus the
 * boxes' candidate-level ranges (lo < value <= hi on Offset,
 * Offset_from_faulting, Delta). See native_tree_eval.h. */
#define NATIVE_TREE_FORMAT 3
#ifndef ST_MODEL_TYPES
#define ST_MODEL_TYPES
/* integer bounds: no floating point (usable in the kernel) */
#define ST_INTEGER_BOUNDS 1
typedef int64_t st_val_t;
#define ST_VAL_MIN ((st_val_t)(-9223372036854775807LL - 1))
#define ST_VAL_MAX ((st_val_t)9223372036854775807LL)
typedef struct {
    st_val_t off_lo, off_hi;    /* Offset (pointer slot index; -1 for spatial) */
    st_val_t ofl_lo, ofl_hi;    /* Offset_from_faulting */
    st_val_t d_lo, d_hi;        /* Delta, in pages */
} st_box_t;
typedef struct {
    uint32_t (*fault)(uint64_t pc, uint64_t prev_pc, st_val_t prev_delta);
    const st_box_t *boxes;
    int nboxes;
} st_model_t;
#endif

/* no spatial trees were trained (no PC reached min_support) */
static inline int st_spatial_lookup(uint64_t pc, st_model_t *model)
{
    (void)pc; (void)model;
    return 0;
}
