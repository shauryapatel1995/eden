"""Parity test: STreeD tree predictions in Python must match the generated C.

Builds a small synthetic labeled dataset, trains per-PC models through the
real pipeline (main.main(), which fits+converts each PC inside its own
subprocess - see main.py's module docstring for why), and checks that
native_eval_tree()'s output in C matches the model's own predictions in
Python for every row. The fitted model objects never leave their
subprocess (unpicklable C++ internals), so "Python's predictions" here are
whatever main.py's workers computed and handed back as plain data
(feat_rows/y_pred) at training time, not a fresh model.predict() call made
by this test.

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
from pystreed_to_c import assemble_header_from_c_fragments, FEATURE_INDEX, ADDRESS_FEATURES

ADDRESS_IDX = {FEATURE_INDEX[f] for f in ADDRESS_FEATURES}

# tools/fltrace/ - one level up from tools/fltrace/single_tree_codegen/ -
# where native_tree_eval.h (the shared tree-walk header production code and
# this test both compile against) lives.
EDEN_FLTRACE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# Real 47-bit user-space addresses, as PC/Prev_PC are in production: two
# "libraries" ~2MB apart, and Prev_PC values only 16-64 bytes apart - far
# closer than a float32 can tell apart at this magnitude (8MB). Labels depend
# on *which* nearby Prev_PC preceded the fault, so trees have to split on
# Prev_PC, and the C side only matches if it compares exact addresses.
LIB_A = 0x7ffff54f0000
LIB_B = 0x7ffff5700000
SPATIAL_PCS = [LIB_A + 0xd613, LIB_B + 0x9afb]
POINTER_PCS = [LIB_A + 0xb59a, LIB_B + 0x6aeb]
PREV_PCS = [LIB_A + 0x1000, LIB_A + 0x1010, LIB_A + 0x1040,
            LIB_B + 0x2000, LIB_B + 0x2020, LIB_B + 0x2060]
HOT_PREV_PCS = {LIB_A + 0x1010, LIB_B + 0x2020}   # neighbours of these are cold
ROWS_PER_PC = 400


def _make_synthetic_dataset(rng: random.Random) -> pd.DataFrame:
    rows = []
    for pc in SPATIAL_PCS:
        for _ in range(ROWS_PER_PC):
            prev_pc = rng.choice(PREV_PCS)
            delta = rng.choice([-3, -2, -1, 0, 1, 2, 3, 4]) * 4096
            prev_delta = rng.choice([-3, -2, -1, 0, 1, 2, 3, 4]) * 4096
            # give the tree real splits to learn instead of one leaf
            color = "green" if (prev_pc in HOT_PREV_PCS and delta <= 4096) else "red"
            rows.append({
                "PC": hex(pc)[2:], "Offset": rng.randint(0, 63), "Delta": delta,
                "Offset_from_faulting": rng.randint(-8, 8),
                "Prev_PC": hex(prev_pc)[2:], "Prev_delta": prev_delta,
                "Type": 0, "Color": color,
            })
    for pc in POINTER_PCS:
        for _ in range(ROWS_PER_PC):
            offset = rng.randint(-32, 32)
            # pointer deltas well past 16MB too, where a float32 of the byte
            # difference would no longer be a whole number of pages
            delta = rng.choice([-70000, -3, -1, 1, 2, 5000, 90000]) * 4096
            offset_from_faulting = rng.randint(-16, 16)
            prev_pc = rng.choice(PREV_PCS)
            color = "green" if (offset <= 4 and prev_pc in HOT_PREV_PCS and delta > -4096) else "red"
            rows.append({
                "PC": hex(pc)[2:], "Offset": offset, "Delta": delta,
                "Offset_from_faulting": offset_from_faulting,
                "Prev_PC": hex(prev_pc)[2:], "Prev_delta": rng.choice([-1, 0, 1]) * 4096,
                "Type": 1, "Color": color,
            })
    return pd.DataFrame(rows)


UNSEEN_PREV_PC = LIB_A + 0x1008   # 8 bytes from a training address, never seen itself


def _ref_walk(node_list: list, feat: list) -> int:
    """Reference tree walk over walk_tree()'s node list, with the semantics
    the generated code must reproduce: numeric splits go left on
    value <= threshold, address splits on set membership, and an address in
    neither set predicts "don't prefetch"."""
    i = 0
    while node_list[i][0] >= 0:
        feature, threshold, left, right, _leaf, lset, rset = node_list[i]
        if lset is not None or rset is not None:
            v = int(feat[feature])
            if v in set(lset or []):
                i = left
            elif v in set(rset or []):
                i = right
            else:
                return 0
        else:
            i = left if feat[feature] <= threshold else right
    return int(float(node_list[i][4]))


def _generate_test_c(category: str, header_code: str, test_rows: list) -> str:
    """C harness: for each row, the same steps the runtime takes - look the
    PC's model up, resolve the fault-level features into the active-box mask
    (model.fault()), then test the candidate-level features (st_predict())."""
    fi = FEATURE_INDEX
    lines = []
    lines.append('#include <stdint.h>')
    lines.append('#include <stdio.h>')
    lines.append(f'#include "native_tree_model_test_{category}.h"')
    lines.append('#include "native_tree_eval.h"')
    lines.append('')
    lines.append('typedef struct { uint64_t pc, prev_pc; double prev_delta, offset, ofl, delta; } test_row_t;')
    lines.append('')
    lines.append('static const test_row_t test_rows[] = {')
    for pc, feat in test_rows:
        lines.append(f"    {{0x{pc:x}ULL, 0x{int(feat[fi['Prev_PC']]):x}ULL, {float(feat[fi['Prev_delta']])!r}, "
                     f"{float(feat[fi['Offset']])!r}, {float(feat[fi['Offset_from_faulting']])!r}, "
                     f"{float(feat[fi['Delta']])!r}}},")
    lines.append('};')
    lines.append('#define NUM_TEST_ROWS (sizeof(test_rows) / sizeof(test_rows[0]))')
    lines.append('')
    lines.append('int main(void) {')
    lines.append('    for (size_t i = 0; i < NUM_TEST_ROWS; i++) {')
    lines.append('        const test_row_t *r = &test_rows[i];')
    lines.append('        st_model_t m;')
    lines.append(f'        if (!st_{category}_lookup(r->pc, &m)) {{ printf("0\\n"); continue; }}')
    lines.append('        uint32_t active = m.fault(r->pc, r->prev_pc, r->prev_delta);')
    lines.append('        printf("%d\\n", st_predict(&m, active, r->offset, r->ofl, r->delta));')
    lines.append('    }')
    if category == "pointer":
        # the slot scan must find exactly what scoring all 512 slots finds
        lines.append('    uint64_t page[512], out[512], seed = 88172645463325252ULL;')
        lines.append('    const uint64_t page_addr = 0x7fffd0000000ULL;')
        lines.append('    const uint64_t prevs[] = {%s};' % ", ".join(f"0x{v:x}ULL" for v in PREV_PCS))
        lines.append('    int scan_bad = 0, scan_cases = 0;')
        lines.append('    for (size_t t = 0; t < NUM_TEST_ROWS; t++) {')
        lines.append('        st_model_t m;')
        lines.append('        if (!st_pointer_lookup(test_rows[t].pc, &m)) continue;')
        lines.append('        for (int rep = 0; rep < 20; rep++) {')
        lines.append('            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;')
        lines.append('            int fl = (int)(seed % 512);')
        lines.append('            uint32_t active = m.fault(test_rows[t].pc, prevs[seed % 6], (double)((int)((seed >> 20) % 3) - 1));')
        lines.append('            for (int i = 0; i < 512; i++) {')
        lines.append('                seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;')
        lines.append('                long pages = (long)(seed % 200001) - 100000;')
        lines.append('                page[i] = (seed & 7) == 0 ? 0 : page_addr + pages * 4096 + (seed >> 40) % 4096;')
        lines.append('            }')
        lines.append('            int nread, n = st_pointer_scan(&m, active, fl, page, page_addr, 4096, out, &nread), k = 0;')
        lines.append('            for (int i = 0; i < 512; i++) {')
        lines.append('                if (!page[i]) continue;')
        lines.append('                uint64_t pp = page[i] & ~4095ULL;')
        lines.append('                double d = (double)(((int64_t)pp - (int64_t)page_addr) / 4096);')
        lines.append('                if (st_predict(&m, active, i, i - fl, d)) { if (k >= n || out[k] != pp) scan_bad++; k++; }')
        lines.append('            }')
        lines.append('            if (k != n) scan_bad++;')
        lines.append('            scan_cases++;')
        lines.append('        }')
        lines.append('    }')
        lines.append('    fprintf(stderr, "scan_check %d %d\\n", scan_cases, scan_bad);')
    lines.append('    return 0;')
    lines.append('}')

    return "\n".join(lines) + "\n", header_code


def run_parity_check() -> bool:
    rng = random.Random(1234)
    tmp_dir = tempfile.mkdtemp(prefix="single_tree_parity_")
    csv_path = os.path.join(tmp_dir, "synthetic.csv")
    _make_synthetic_dataset(rng).to_csv(csv_path, index=False)

    # {'spatial': {pc: (c_code, num_nodes, feat_rows, y_pred)}, 'pointer': {...}}
    # - each PC's fit()+convert already happened inside its own subprocess
    # (see main.py); feat_rows/y_pred are that same subprocess's own
    # held-out predictions, handed back as plain data since the model
    # object itself can't cross the process boundary.
    pc_code_map = train_models(csv_path, top_n=len(SPATIAL_PCS) + len(POINTER_PCS), min_support=0,
                                max_depth=3, max_num_nodes=7)

    all_ok = True
    for category in ("spatial", "pointer"):
        entries = pc_code_map.get(category, {})
        if not entries:
            print(f"[{category}] no models trained, skipping")
            continue

        assemble_entries = {pc: (e[0], e[1]) for pc, e in entries.items()}
        header_code = assemble_header_from_c_fragments(assemble_entries, category)

        test_rows = []       # [(pc_int, feat), ...]
        expected = []        # [0/1, ...] aligned with test_rows
        for pc, (_c_code, _num_nodes, feat_rows, y_pred, node_list) in entries.items():
            pc_int = int(pc, 16) if isinstance(pc, str) else int(pc)
            for feat, pred in zip(feat_rows, y_pred):
                if _ref_walk(node_list, feat) != int(pred):
                    print(f"[{category}] reference walker disagrees with pystreed at pc=0x{pc_int:x}")
                    all_ok = False
                test_rows.append((pc_int, feat))
                expected.append(int(pred))
                unseen = list(feat)
                unseen[FEATURE_INDEX['Prev_PC']] = UNSEEN_PREV_PC
                test_rows.append((pc_int, unseen))
                expected.append(_ref_walk(node_list, unseen))

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
        for line in run_result.stderr.splitlines():
            if line.startswith("scan_check"):
                _, cases, bad = line.split()
                if int(bad):
                    print(f"[{category}] slot scan: {bad} of {cases} random pages disagree with scoring all 512 slots")
                    all_ok = False
                else:
                    print(f"[{category}] slot scan OK - {cases} random pages match scoring all 512 slots")
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
                  f"across {len(entries)} PC(s)")

    return all_ok


if __name__ == "__main__":
    ok = run_parity_check()
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
