"""Parity test: STreeD tree predictions in Python must match the generated C.

Builds a small synthetic labeled dataset, trains per-PC models through the
real pipeline (main.load_and_filter_data via main.main(), preprocessing.py),
converts them to C with pystreed_to_c.convert_models_to_c_code(), compiles
that C against the real ../native_tree_eval.h (tools/fltrace/native_tree_eval.h),
and checks that native_eval_tree()'s output in C matches model.predict() in
Python for every row.

Run directly: `python test_c_parity.py`
"""
import os
import random
import subprocess
import sys
import tempfile

import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from main import main as train_models
from preprocessing import load_and_filter_data
from pystreed_to_c import convert_models_to_c_code, fmt_float, FEATURE_INDEX

# tools/fltrace/ - one level up from tools/fltrace/single_tree_codegen/ -
# where native_tree_eval.h (the shared tree-walk header production code and
# this test both compile against) lives.
EDEN_FLTRACE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# Kept well under 2**24: native_tree_node_t's `threshold` (and the native
# feat[] array) are `float` (32-bit, ~7 decimal digits), so real 47-bit
# addresses (~1.4e14, as PC/Prev_PC are in production) round nearby
# thresholds together and lose splits - a pre-existing, accepted limitation
# of the native format (see the FeatureVector comment in prefetch.h), not
# something this test is meant to catch. Keeping synthetic PCs small keeps
# this test focused on conversion-logic correctness (indexing, left/right
# branch semantics, per-PC routing) instead of that separate, known
# precision cliff.
SPATIAL_PCS = [0x1000, 0x2000]
POINTER_PCS = [0x3000, 0x4000]
ROWS_PER_PC = 300


def _make_synthetic_dataset(rng: random.Random) -> pd.DataFrame:
    rows = []
    for pc in SPATIAL_PCS:
        for _ in range(ROWS_PER_PC):
            prev_pc = pc + rng.choice([-0x40, -0x10, 0, 0x10, 0x40, 0x80])
            delta = rng.choice([-3, -2, -1, 0, 1, 2, 3, 4]) * 4096
            prev_delta = rng.choice([-3, -2, -1, 0, 1, 2, 3, 4]) * 4096
            # give the tree real splits to learn instead of one leaf
            color = "green" if (delta <= 0 and prev_delta > -4096) else "red"
            rows.append({
                "PC": pc, "Offset": rng.randint(0, 63), "Delta": delta,
                "Offset_from_faulting": rng.randint(-8, 8),
                "Prev_PC": hex(prev_pc)[2:], "Prev_delta": prev_delta,
                "Type": 0, "Color": color,
            })
    for pc in POINTER_PCS:
        for _ in range(ROWS_PER_PC):
            offset = rng.randint(-32, 32)
            delta = rng.choice([-3, -2, -1, 0, 1, 2, 3, 4]) * 4096
            offset_from_faulting = rng.randint(-16, 16)
            prev_pc = pc + rng.choice([-0x40, -0x10, 0, 0x10, 0x40, 0x80])
            color = "green" if (offset <= 4 and offset_from_faulting > -2) else "red"
            rows.append({
                "PC": pc, "Offset": offset, "Delta": delta,
                "Offset_from_faulting": offset_from_faulting,
                "Prev_PC": hex(prev_pc)[2:], "Prev_delta": rng.choice([-1, 0, 1]) * 4096,
                "Type": 1, "Color": color,
            })
    return pd.DataFrame(rows)


def _build_feat_rows(X: pd.DataFrame, norm_cache: dict) -> list:
    """Reconstruct the *raw* 6-wide feature vectors native_eval_tree expects
    from the (partially normalized) X the model was trained/predicted on."""
    mean, std = norm_cache.get("Prev_PC", (0.0, 1.0))
    feat_rows = []
    for _, row in X.iterrows():
        feat = [0.0] * len(FEATURE_INDEX)
        for col, idx in FEATURE_INDEX.items():
            if col not in X.columns:
                continue
            v = row[col]
            if col == "Prev_PC":
                v = v * std + mean  # invert the z-score normalization
            feat[idx] = float(v)
        feat_rows.append(feat)
    return feat_rows


def _generate_test_c(category: str, header_code: str, test_rows: list) -> str:
    lines = []
    lines.append('#include <stdint.h>')
    lines.append('#include <stdio.h>')
    lines.append(f'#include "native_tree_model_test_{category}.h"')
    lines.append('#include "native_tree_eval.h"')
    lines.append('')
    lines.append('typedef struct { uint64_t pc; float feat[%d]; } test_row_t;' % len(FEATURE_INDEX))
    lines.append('')
    lines.append('static const test_row_t test_rows[] = {')
    for pc, feat in test_rows:
        feat_str = ", ".join(fmt_float(v) for v in feat)
        lines.append(f'    {{0x{pc:x}ULL, {{{feat_str}}}}},')
    lines.append('};')
    lines.append('#define NUM_TEST_ROWS (sizeof(test_rows) / sizeof(test_rows[0]))')
    lines.append('')
    lines.append('int main(void) {')
    lines.append('    for (size_t i = 0; i < NUM_TEST_ROWS; i++) {')
    lines.append('        int num_nodes;')
    lines.append('        const native_tree_node_t *nodes = get_native_nodes(test_rows[i].pc, &num_nodes);')
    lines.append('        if (!nodes) { printf("-1\\n"); continue; }')
    lines.append('        printf("%d\\n", (int)native_eval_tree(nodes, test_rows[i].feat));')
    lines.append('    }')
    lines.append('    return 0;')
    lines.append('}')

    return "\n".join(lines) + "\n", header_code


def run_parity_check() -> bool:
    rng = random.Random(1234)
    tmp_dir = tempfile.mkdtemp(prefix="single_tree_parity_")
    csv_path = os.path.join(tmp_dir, "synthetic.csv")
    _make_synthetic_dataset(rng).to_csv(csv_path, index=False)

    pc_to_model_map, pc_to_norm_cache_map = train_models(
        csv_path, top_n=len(SPATIAL_PCS) + len(POINTER_PCS), min_support=0)
    spatial_datasets, pointer_datasets = load_and_filter_data(
        csv_path, top_n=len(SPATIAL_PCS) + len(POINTER_PCS), min_support=0)
    datasets_by_category = {"spatial": spatial_datasets, "pointer": pointer_datasets}

    all_ok = True
    for category in ("spatial", "pointer"):
        models = pc_to_model_map.get(category, {})
        if not models:
            print(f"[{category}] no models trained, skipping")
            continue

        header_code = convert_models_to_c_code({category: models}, pc_to_norm_cache_map)

        test_rows = []       # [(pc_int, feat), ...]
        expected = []        # [0/1, ...] aligned with test_rows
        for data_object in datasets_by_category[category]:
            pc = data_object["pc"]
            X = data_object["X"]
            norm_cache = data_object["feature_normalization_cache"]
            model = models[pc]

            y_pred = model.predict(X)
            feat_rows = _build_feat_rows(X, norm_cache)
            pc_int = int(pc)
            for feat, pred in zip(feat_rows, y_pred):
                test_rows.append((pc_int, feat))
                expected.append(int(pred))

        c_code, header_code = _generate_test_c(category, header_code, test_rows)

        header_path = os.path.join(tmp_dir, f"native_tree_model_test_{category}.h")
        with open(header_path, "w") as f:
            f.write(header_code)
        c_path = os.path.join(tmp_dir, f"test_main_{category}.c")
        with open(c_path, "w") as f:
            f.write(c_code)

        bin_path = os.path.join(tmp_dir, f"test_bin_{category}")
        compile_cmd = ["gcc", "-O2", "-Wall", "-Wextra", "-I", tmp_dir, "-I", EDEN_FLTRACE_DIR,
                       "-o", bin_path, c_path, "-lm"]
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"[{category}] COMPILE FAILED:\n{result.stdout}\n{result.stderr}")
            all_ok = False
            continue

        run_result = subprocess.run([bin_path], capture_output=True, text=True)
        c_preds = [int(line) for line in run_result.stdout.splitlines()]

        if len(c_preds) != len(expected):
            print(f"[{category}] row count mismatch: C produced {len(c_preds)}, "
                  f"expected {len(expected)}")
            all_ok = False
            continue

        mismatches = [(i, test_rows[i][0], expected[i], c_preds[i])
                      for i in range(len(expected)) if expected[i] != c_preds[i]]
        if mismatches:
            print(f"[{category}] {len(mismatches)}/{len(expected)} predictions disagree, e.g.:")
            for i, pc, py_pred, c_pred in mismatches[:10]:
                print(f"    row {i} pc=0x{pc:x} python={py_pred} c={c_pred}")
            all_ok = False
        else:
            print(f"[{category}] OK - {len(expected)}/{len(expected)} predictions match "
                  f"across {len(models)} PC(s)")

    return all_ok


if __name__ == "__main__":
    ok = run_parity_check()
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
