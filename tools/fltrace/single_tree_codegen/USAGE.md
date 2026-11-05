# Usage

## Prerequisites

The pipeline requires a Python environment with the modified PySTreeD library
(in `pystreed_src/`) installed. The environment is managed with
[uv](https://docs.astral.sh/uv/).

## Building PySTreeD from source

The `pystreed_src/` directory contains a modified copy of PySTreeD with
native F-beta score support and Pareto front access. After any C++ change in
that directory, rebuild and install with:

```sh
uv pip install -e pystreed_src/
```

This compiles the C++17 extension via pybind11 and installs the package in
development mode. The build takes ~2 minutes. A C++17-capable compiler
(g++ 8+ or clang 7+) is required.

## Running the codegen pipeline

```sh
python gen_single_tree_code.py <dataset.csv> --min_support 100
```

Or use `--top_n N` to keep the N most frequent PCs instead of filtering by
minimum positive-sample support.

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `--min_support N` | 0 | Keep PCs with at least N positive ("green") samples |
| `--top_n N` | 0 | Keep the N most frequent PCs (mutually exclusive with `--min_support`) |
| `--max_depth N` | 4 | Maximum tree depth passed to `STreeDClassifier` |
| `--max_num_nodes N` | 10 | Maximum number of branching nodes per tree |
| `--f_beta B` | 1.0 | Beta for F-beta score optimization (see below) |
| `--out_dir DIR` | `tools/fltrace/` | Where to write the generated `.h` files |

### F-beta score

The `--f_beta` flag controls the precision/recall tradeoff of every per-PC
tree. The solver optimizes F-beta = (1+B^2)*TP / ((1+B^2)*TP + B^2*FN + FP)
natively in C++, so there is no data duplication or runtime overhead compared
to the default F1.

| Value | Effect |
|-------|--------|
| `0.5` | Precision-weighted — fewer false positives, more conservative prefetching |
| `1.0` | Standard F1 — equal weight to precision and recall (default) |
| `2.0` | Recall-weighted — fewer false negatives, more aggressive prefetching |

Example:

```sh
python gen_single_tree_code.py traces/mcf.csv --min_support 100 --f_beta 0.5
```

### Pareto front access (Python API)

When using the `f1-score` optimization task, the solver maintains a Pareto
front over (FN, FP). After fitting, you can inspect all Pareto-optimal
solutions:

```python
from pystreed import STreeDClassifier

clf = STreeDClassifier("f1-score", max_depth=3, f_beta=0.5)
clf.fit(X_train, y_train)

# List all Pareto-optimal solutions
for sol in clf.pareto_front():
    print(f"index={sol['index']}  score={sol['score']:.4f}")

# Switch to a different solution from the front
clf.select_solution(0)
preds = clf.predict(X_test)
```

## Output

The pipeline writes two header files:

- `native_tree_model_spatial.h` — trees for spatial/next-N candidates (Type=0)
- `native_tree_model_pointer.h` — trees for pointer-chase candidates (Type=1)

These are included by `single_tree_predict_{spatial,pointer}.c` and compiled
into the runtime prefetch predictor. **Regenerate whenever the training data
changes** — the model is baked in at compile time.

## Expected CSV columns

`PC, Offset, Delta, Offset_from_faulting, Prev_PC, Prev_delta, Type, Color`

- `Type`: `0` for spatial, `1` for pointer
- `Color`: `green` = worth prefetching (label 1), anything else = label 0
- `PC`/`Prev_PC`: hex string or int
- `Delta`/`Prev_delta`: raw byte deltas (divided by 4096 during preprocessing)

## Testing

```sh
python test_c_parity.py
```

Trains models on synthetic data, generates C, compiles against the real
`../native_tree_eval.h`, and diffs every prediction against Python's
`model.predict()`.
