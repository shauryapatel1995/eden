"""Train per-PC STreeD models and generate native_tree_model_{spatial,pointer}.h.

Thin driver that wires together the training loop in main.py (which uses
preprocessing.py to build one dataset per PC) and the codegen in
pystreed_to_c.py, and writes the two headers single_tree_predict_spatial.c /
single_tree_predict_pointer.c include. main.py/preprocessing.py/
pystreed_to_c.py live alongside this file (tools/fltrace/single_tree_codegen/)
so the whole pipeline runs from inside eden with no dependency on files
outside this repo.

Usage:
    python gen_single_tree_code.py <dataset.csv> [--top_n N | --min_support N] [--out_dir DIR]
"""
import argparse
import os

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
# native_tree_model_{spatial,pointer}.h need to land next to
# single_tree_predict_{spatial,pointer}.c, one level up from this script
_DEFAULT_OUT_DIR = os.path.dirname(_THIS_DIR)

from main import main as train_models  # noqa: E402
from pystreed_to_c import assemble_header_from_c_fragments  # noqa: E402


def generate(filename: str, top_n: int, min_support: int, out_dir: str) -> None:
    # {'spatial': {pc: (c_code_str, num_nodes, feat_rows, y_pred)}, 'pointer':
    # {...}} - each PC's tree was already fit and converted to C inside its
    # own subprocess (see main.py), so there's no model object left to walk
    # here, just fragments to assemble. feat_rows/y_pred are only there for
    # test_c_parity.py; the real codegen path only needs (c_code, num_nodes).
    pc_code_map = train_models(filename, top_n, min_support)

    os.makedirs(out_dir, exist_ok=True)
    for category in ("spatial", "pointer"):
        entries = {pc: (c_code, num_nodes)
                   for pc, (c_code, num_nodes, *_rest) in pc_code_map.get(category, {}).items()}
        if not entries:
            print(f"no {category} models trained, skipping")
            continue
        c_code = assemble_header_from_c_fragments(entries, category)
        out_path = os.path.join(out_dir, f"native_tree_model_{category}.h")
        with open(out_path, "w") as f:
            f.write(c_code)
        print(f"wrote {out_path} ({len(entries)} PCs)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("filename", type=str, help="Path to the dataset file")
    parser.add_argument("--top_n", type=int, default=0, help="Number of top PCs to filter by occurrence")
    parser.add_argument("--min_support", type=int, default=0, help="Minimum support for filtering PCs")
    parser.add_argument("--out_dir", type=str, default=_DEFAULT_OUT_DIR,
                         help="Directory to write native_tree_model_{spatial,pointer}.h into "
                              "(default: tools/fltrace/, alongside single_tree_predict_*.c)")
    args = parser.parse_args()

    generate(args.filename, args.top_n, args.min_support, args.out_dir)
