# single_tree_codegen

Trains one small [PySTreeD](https://github.com/AlgTUDelft/pystreed) decision
tree per hot PC and compiles each tree straight into a C node array, so the
runtime prefetch predictor in `tools/fltrace/single_tree_predict_{spatial,pointer}.c`
can walk it with no ML runtime linked in at all.

This replaces the older XGBoost-ensemble path (`gen_tree_code.py`,
`native_predict_{pointer,spatial}.c`, one shared ensemble across all PCs).
The single-tree approach trains a separate, tiny tree per PC instead, which
in practice fits each PC's local access pattern better and is cheap enough
to walk per-candidate at runtime.

## Pipeline

```
your_trace.csv
      |
      v
preprocessing.py   -- load_and_filter_data(): pick the hottest PCs, split
      |                spatial (Type=0) vs pointer (Type=1) rows, normalize
      |                features per PC
      v
main.py            -- train one STreeDClassifier per (category, PC)
      |
      v
pystreed_to_c.py    -- convert_models_to_c_code(): walk each tree, denormalize
      |                its thresholds, emit one native_nodes_<category>_<pc>[]
      |                array per PC plus a get_native_nodes(pc) router
      v
gen_single_tree_code.py  -- driver: ties the above together, writes
      |                      native_tree_model_{spatial,pointer}.h
      v
../native_tree_model_{spatial,pointer}.h
      |
      v
../single_tree_predict_{spatial,pointer}.c   -- at runtime: get_native_nodes(pc)
                                                 to find the candidate's tree,
                                                 then ../native_tree_eval.h's
                                                 native_eval_tree() walks it
```

## Usage

```sh
python gen_single_tree_code.py <dataset.csv> --min_support 100
# or: --top_n N  to just take the N most frequent PCs instead
```

Writes `native_tree_model_spatial.h` and `native_tree_model_pointer.h` into
`tools/fltrace/` (one level up - alongside the `.c` files that include them)
by default. Pass `--out_dir` to write somewhere else.

**Regenerate these two headers whenever the training dataset changes** - they're
committed (like the old ensemble headers were) since `single_tree_predict_*.c`
bakes the model in at compile time; there's no runtime model-loading path.

### Expected CSV columns

`PC, Offset, Delta, Offset_from_faulting, Prev_PC, Prev_delta, Type, Color`

- `Type`: `0` for spatial/next-N candidates, `1` for pointer-chase candidates
  - trained as two independent models because miss:hit class balance differs
    drastically between the two (~6:1 vs ~1400:1 on the mcf training set) -
    one shared model tuned for one ratio predicts positive on ~none of the
    other's candidates
- `Color`: `green` = worth prefetching (label 1), anything else = label 0
- `PC` / `Prev_PC`: hex string or int; `Delta` / `Prev_delta`: raw byte deltas
  (divided by 4096 during preprocessing to match the page-count units
  `FeatureVector.delta`/`prev_delta` arrive in at runtime - see the comment
  in `single_tree_predict_pointer.c`)

### Filtering which PCs get a tree

`--min_support N` keeps every PC with at least N "green" (positive) samples;
`--top_n N` instead just keeps the N most frequent PCs regardless of label
balance. Use exactly one. A PC that isn't kept gets no tree - at runtime,
`get_native_nodes()` returns `NULL` for it and the candidate is treated as
"not prefetchable" without ever walking a tree.

## How the C side consumes this

`native_tree_model_{spatial,pointer}.h` each define:
- `native_tree_node_t` - `{feature, threshold, left, right, missing_left, leaf_value}`
- one `static const native_tree_node_t native_nodes_<category>_<pc>[]` per trained PC
- `get_native_nodes(uint64_t pc, int *num_nodes)` - routes a PC to its array, or `NULL`

`native_tree_eval.h` (one level up, shared by both `.c` files and this
package's test) defines `native_eval_tree(nodes, feat)`, which walks a node
array node-by-node: `go_left` iff `feat[node.feature] <= node.threshold`
(or `node.missing_left` if the feature is NaN), stopping at a leaf
(`feature == -1`) and returning its `leaf_value` (0.0f/1.0f).

`single_tree_predict_{spatial,pointer}.c`'s `native_predict_batch_*()` calls
`get_native_nodes(pc, ...)` per candidate, skips it if `NULL`, and otherwise
builds the 6-wide `feat[]` array and calls `native_eval_tree()`.

This only compiles under `-DDO_PREFETCH_SINGLE_TREE` (opt-in, mutually
exclusive with the XGBoost-ensemble path).

## Left/right branch semantics (read before touching `walk_tree`)

PySTreeD's own `node.left_child`/`right_child` are the *reverse* of what
their names suggest: `right_child` is the "predicate satisfied"
(`feature <= threshold`) branch, `left_child` is "not satisfied" - confirmed
via `STreeDClassifier._recursive_print_tree`/`_recursive_export_dot` in
pystreed's `base.py`, which print `left_child` under "not satisfied" and
`right_child` under "satisfied". `native_eval_tree()`'s C `'left'` slot is
walked when `v <= threshold`, so `walk_tree()` has to swap them: it assigns
`right_child` to `'left'` and `left_child` to `'right'`. Getting this backwards
silently inverts predictions instead of erroring - `test_c_parity.py` is what
originally caught it.

Node IDs are also assigned by append order (reserve a slot, recurse, use
whatever index recursion lands on), not by an `id+1`/`id+2` formula - that
formula only avoids collisions for a perfectly balanced tree of uniform
depth, and STreeD trees are generally unbalanced.

## Known limitation: float32 thresholds at address-scale magnitudes

`native_tree_node_t.threshold` and the runtime `feat[]` array are both
`float` (32-bit, ~7 decimal digits of precision). Real `Prev_PC` values are
~47-bit addresses (~1.4e14) - float32 can't distinguish two such addresses
that differ by less than roughly a million, so a tree with a fine-grained
`Prev_PC` split can lose that split's precision when denormalized back into
raw address space and cast down to `float`. This is a pre-existing,
accepted tradeoff of the whole native (non-XGBoost-library) prediction path
(see the `FeatureVector` comment in `inc/rmem/prefetch.h`), not something
introduced or fixed here - just worth knowing if a `Prev_PC`-heavy model's
native accuracy doesn't match its Python accuracy.

## Testing

```sh
python test_c_parity.py
```

Trains real models on a small synthetic dataset (through this same
pipeline), generates C, compiles it against the real `../native_tree_eval.h`,
and diffs every prediction against `model.predict()` in Python. This is what
would have caught both bugs described above.
