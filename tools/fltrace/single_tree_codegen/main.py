"""Main script for data processing and model training.

Accepts as input a filename containing the dataset and a number N to filter
the top N PCs by occurrence.
Then processes the data, trains a STreeDClassifier model for each PC,
and generates C code for predictions.
Saves the generated C code to output files.

Args:
    filename: Path to the dataset file
    top_n: Number of top PCs to filter by occurrence
"""
import argparse
import multiprocessing
import os
import pickle
import tempfile
from pystreed import STreeDClassifier
from preprocessing import load_and_filter_data
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report
from pystreed_to_c import convert_tree_to_c_code, walk_tree, denormalize_feat_rows

# STreeDClassifier's fitted model wraps a C++ solver result that can't be
# pickled (confirmed: pickling raises "cannot pickle
# 'pystreed.cstreed.SolverResult' object"), so a crashing fit() can't be
# isolated by just running it in a subprocess and sending the model object
# back - there'd be nothing picklable to send. Instead each worker does
# fit() THROUGH to the final C code string (which is a plain, always-
# picklable str) entirely inside its own subprocess; only that string (or a
# failure marker) ever crosses the process boundary. A worker segfaulting
# (see MAX_DEPTH's docstring below) then only loses that one PC's tree
# instead of aborting hours of training on every other PC.
MAX_DEPTH = 3
MAX_NUM_NODES = 5

# Result handoff is a pickle file, NOT a multiprocessing.Queue. Queue is
# backed by an OS pipe with a small kernel buffer (~64KB); once a PC's
# held-out feat_rows/y_pred payload exceeds that (easily happens with the
# full 6-feature set on a multi-million-row PC), the child blocks in
# queue.put() waiting for the parent to drain the pipe while the parent is
# blocked in p.join() waiting for the child to exit first - a real deadlock
# that was actually hit (confirmed stuck at 0% CPU for 20+ hours on PC
# 555555555b82's 2.9M-row spatial dataset). A file has no such size limit;
# the parent only reads it after p.join() confirms the child is done.
_RESULT_PATH = os.path.join(tempfile.gettempdir(), f"single_tree_worker_result_{os.getpid()}.pkl")


def _fit_and_convert_worker(pc, X, y, category, feature_normalization_cache, result_path):
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)
    model = STreeDClassifier("f1-score", max_depth=MAX_DEPTH, max_num_nodes=MAX_NUM_NODES)
    model.fit(X_train, y_train)

    y_pred = model.predict(X_test)
    report = classification_report(y_test, y_pred)

    try:
        c_code = convert_tree_to_c_code(int(pc, 16) if isinstance(pc, str) else int(pc),
                                         model, feature_normalization_cache, category)
    except ValueError as e:
        # a "== value" or "in [...]" split (see convert_tree_to_c_code's own
        # docstring) - a real pystreed output the native <=-only tree format
        # can't represent, not a crash. Distinct from a native segfault
        # (which never reaches here at all) so the caller can log it as a
        # clean skip rather than "likely a pystreed native crash".
        with open(result_path, "wb") as f:
            pickle.dump(('unsupported_split', str(e)), f)
        return
    num_nodes = len(walk_tree(model, feature_normalization_cache))
    # test_c_parity.py needs "Python's own predictions" to diff the C output
    # against, but the fitted model object can't leave this subprocess (see
    # module docstring) - hand back the plain, picklable ingredients instead
    # (raw/denormalized feature rows + the model's own predictions on them),
    # computed here where the model still exists.
    feat_rows = denormalize_feat_rows(X_test, feature_normalization_cache)
    with open(result_path, "wb") as f:
        pickle.dump(('ok', report, c_code, num_nodes, feat_rows, [int(p) for p in y_pred]), f)


def main(filename: str, top_n: int, min_support: int):
    # Load and filter data
    spatial_datasets, pointer_datasets = load_and_filter_data(filename, top_n, min_support)
    pc_datasets = [(dataset, 'spatial') for dataset in spatial_datasets] + \
            [(dataset, 'pointer') for dataset in pointer_datasets]

    pc_code_spatial = {}
    pc_code_pointer = {}
    for i, (data_object, ds_type) in enumerate(pc_datasets):
        pc = data_object['pc']
        X = data_object['X']
        y = data_object['y']
        feature_normalization_cache = data_object['feature_normalization_cache']
        print(f"{i}: Processing PC {pc} with {len(X)} samples", flush=True)

        # NOTE(2026-08-18/19): max_depth>=3 segfaults reproducibly inside
        # pystreed's native fit() for at least some PCs (first found on the
        # random-ll trace at depth 4, reproduces again on real mcf PCs at
        # depth 3 - e.g. PC 7ffff56c627c). This is a pystreed library bug,
        # not an Eden data/logic issue, and it's data-dependent rather than
        # universal (many other PCs fit fine at the same depth) - so each
        # PC's fit()+codegen runs in its own subprocess: a segfault there
        # kills only that subprocess, and this PC is skipped (same "no tree
        # for this PC" outcome as failing the min_support filter) instead of
        # losing the whole run.
        if os.path.exists(_RESULT_PATH):
            os.remove(_RESULT_PATH)  # stale file from a previous PC must not be misread as this PC's result
        p = multiprocessing.Process(target=_fit_and_convert_worker,
                                     args=(pc, X, y, ds_type, feature_normalization_cache, _RESULT_PATH))
        p.start()
        p.join()
        result = None
        if p.exitcode == 0 and os.path.exists(_RESULT_PATH):
            try:
                with open(_RESULT_PATH, "rb") as f:
                    result = pickle.load(f)
            except Exception:
                result = None

        if result is None:
            print(f"SKIPPING PC {pc} ({ds_type}): worker exited with code {p.exitcode} "
                  f"(likely a pystreed native crash) - no tree trained for this PC", flush=True)
            continue
        if result[0] == 'unsupported_split':
            print(f"SKIPPING PC {pc} ({ds_type}): unsupported split ({result[1]})", flush=True)
            continue

        _, report, c_code, num_nodes, feat_rows, y_pred = result
        print(f"Trained model for PC {pc}", flush=True)
        print(report, flush=True)

        entry = (c_code, num_nodes, feat_rows, y_pred)
        if ds_type == 'spatial':
            pc_code_spatial[pc] = entry
        elif ds_type == 'pointer':
            pc_code_pointer[pc] = entry

    # {'spatial': {pc: (c_code_str, num_nodes, feat_rows, y_pred)}, 'pointer':
    # {...}} - already fully converted to C (each PC's fit+convert happened
    # together inside its own subprocess, see above), nothing left for
    # pystreed_to_c.py to walk/convert again. feat_rows/y_pred (the model's
    # own held-out predictions, in plain picklable form) are only consumed
    # by test_c_parity.py - gen_single_tree_code.py's real codegen path
    # ignores them and only uses (c_code, num_nodes).
    return {'spatial': pc_code_spatial, 'pointer': pc_code_pointer}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Train STreeDClassifier models and generate C code.")
    parser.add_argument("filename", type=str, help="Path to the dataset file")
    parser.add_argument("--top_n", type=int, default=0, help="Number of top PCs to filter by occurrence")
    parser.add_argument("--min_support", type=int, default=0, help="Minimum support for filtering PCs")
    args = parser.parse_args()

    main(args.filename, args.top_n, args.min_support)
