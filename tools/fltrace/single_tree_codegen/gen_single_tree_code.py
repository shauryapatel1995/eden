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
from pystreed_to_c import convert_models_to_c_code  # noqa: E402


def generate(filename: str, top_n: int, min_support: int, out_dir: str) -> None:
    pc_to_model_map, pc_to_normalization_cache_map = train_models(filename, top_n, min_support)

    os.makedirs(out_dir, exist_ok=True)
    for category in ("spatial", "pointer"):
        models = pc_to_model_map.get(category, {})
        if not models:
            print(f"no {category} models trained, skipping")
            continue
        # one category per call - a PC that ended up with both a spatial and
        # a pointer tree would otherwise collide as two `case 0x...:` labels
        # in the same get_native_nodes() switch
        c_code = convert_models_to_c_code({category: models}, pc_to_normalization_cache_map)
        out_path = os.path.join(out_dir, f"native_tree_model_{category}.h")
        with open(out_path, "w") as f:
            f.write(c_code)
        print(f"wrote {out_path} ({len(models)} PCs)")


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
