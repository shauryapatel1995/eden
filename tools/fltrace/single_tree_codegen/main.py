"""Main script for data processing and model training.

Accepts as input a filename containing the dataset and a number N to filter
the top N PCs by occurrence.
Then processes the data, trains a STreeDClassifier model for each PC,
and generates C code for predictions.
Saves the generated C code to output files.

Args:
    filename: Path to the dataset file
    top_n: Number of top PCs to filter by occurrence
    max_depth: Maximum depth of the STreeDClassifier tree
    max_num_nodes: Maximum number of nodes in the STreeDClassifier tree
    f_beta: Beta parameter for F-beta score optimization (default 1.0 = F1)
"""
import argparse
from pystreed import STreeDClassifier
from preprocessing import load_and_filter_data
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, fbeta_score
import numpy as np


def main(filename: str, top_n: int, min_support: int, max_depth: int, max_num_nodes: int, f_beta: float = 1.0):
    # Load and filter data
    spatial_datasets, pointer_datasets = load_and_filter_data(filename, top_n, min_support)
    pc_datasets = [(dataset, 'spatial') for dataset in spatial_datasets] + \
            [(dataset, 'pointer') for dataset in pointer_datasets]

    pc_to_model_spatial = {}
    pc_to_model_pointer = {}
    pc_to_norm_cache_spatial = {}
    pc_to_norm_cache_pointer = {}
    for i, (data_object, ds_type) in enumerate(pc_datasets):
        pc = data_object['pc']
        X = data_object['X']
        y = data_object['y']
        feature_normalization_cache = data_object['feature_normalization_cache']
        print(f"{i}: Processing PC {pc} with {len(X)} samples")

        X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

        model = STreeDClassifier("f1-score", max_depth=max_depth, max_num_nodes=max_num_nodes, f_beta=f_beta)
        model.fit(X_train, y_train)
        print(f"Trained model for PC {pc} (f_beta={f_beta})")

        y_pred = model.predict(X_test)
        print(classification_report(y_test, y_pred))
        print(f"F{f_beta}-score: {fbeta_score(y_test, y_pred, beta=f_beta):.4f}")

        if ds_type == 'spatial':
            pc_to_model_spatial[pc] = model
            pc_to_norm_cache_spatial[pc] = feature_normalization_cache
        elif ds_type == 'pointer':
            pc_to_model_pointer[pc] = model
            pc_to_norm_cache_pointer[pc] = feature_normalization_cache

    pc_to_model_map = {
        'spatial': pc_to_model_spatial,
        'pointer': pc_to_model_pointer
    }
    # feature_normalization_cache is per-PC (each PC's dataset has its own
    # Prev_PC mean/std), so it has to travel alongside pc_to_model_map rather
    # than as a single shared dict - pystreed_to_c.py needs the right cache
    # per (category, pc) to denormalize that tree's thresholds.
    pc_to_normalization_cache_map = {
        'spatial': pc_to_norm_cache_spatial,
        'pointer': pc_to_norm_cache_pointer
    }

    return pc_to_model_map, pc_to_normalization_cache_map


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Train STreeDClassifier models and generate C code.")
    parser.add_argument("filename", type=str, help="Path to the dataset file")
    parser.add_argument("--top_n", type=int, default=0, help="Number of top PCs to filter by occurrence")
    parser.add_argument("--min_support", type=int, default=0, help="Minimum support for filtering PCs")
    parser.add_argument("--max_depth", type=int, default=4, help="Maximum depth of the STreeDClassifier tree")
    parser.add_argument("--max_num_nodes", type=int, default=10, help="Maximum number of nodes in the STreeDClassifier tree")
    parser.add_argument("--f_beta", type=float, default=1.0,
                        help="Beta for F-beta score (0.5 = precision-weighted, 1.0 = F1, 2.0 = recall-weighted)")
    args = parser.parse_args()

    main(args.filename, args.top_n, args.min_support, args.max_depth, args.max_num_nodes, args.f_beta)
