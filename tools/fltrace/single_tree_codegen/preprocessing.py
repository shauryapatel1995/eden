import os
from typing import List, Union, Sequence
import pandas as pd


# Configuration constants
# Same 6-feature set the ensemble pipeline uses for both categories
# (gen_tree_code.py's FEATURE_INDEX) - PC is included for parity even
# though it's constant within any single PC's per-PC dataset here (the
# whole premise of one tree per PC) and so can never actually be split on;
# pystreed's binarizer safely no-ops a zero-variance column (see
# Binarizer.fit() in pystreed/binarizer.py: n_categories<=1 excludes it
# from ever producing a threshold candidate) rather than erroring, so
# including it costs nothing beyond a no-op column.
SPATIAL_COLS = ['PC', 'Offset', 'Delta', 'Offset_from_faulting', 'Prev_PC', 'Prev_delta']
POINTER_COLS = ['PC', 'Offset', 'Delta', 'Offset_from_faulting', 'Prev_PC', 'Prev_delta']
OUTPUT_COL = 'Color'


def hex_to_int(value: Union[str, int]) -> int:
    """
    Convert hex string to int, or return int if already an int.

    Args:
        value: Hex string or integer value

    Returns:
        Integer representation of the value
    """
    return int(value, 16) if isinstance(value, str) else int(value)


def normalize_big_numbers(values: Sequence[Union[str, int, float]]) -> tuple[List[float], float, float]:
    """
    Normalize a list of large numbers using z-score normalization.

    Args:
        values: List of numeric values to normalize

    Returns:
        List of normalized float values
    """
    # Convert all values to integers
    int_values = [int(x) for x in values]

    # Calculate mean and standard deviation
    mean = sum(int_values) / len(int_values)
    variance = sum((addr - mean) ** 2 for addr in int_values) / len(int_values)
    std = variance ** 0.5

    # Avoid division by zero
    if std == 0:
        std = 1.0

    # Return normalized values
    return [(addr - mean) / std for addr in int_values], mean, std


def process_pc_data(pc_data: pd.DataFrame) -> tuple[pd.DataFrame, pd.Series, dict]:
    """
    Process and normalize PC dataset for model training.

    Args:
        pc_data: Raw PC dataset DataFrame

    Returns:
        Processed DataFrame with normalized features
    """
    data = pc_data.copy()

    if data.empty:
        raise ValueError("The provided PC dataset is empty.")

    # Convert and normalize prev_pc column
    prev_pcs = [hex_to_int(pc) for pc in data['Prev_PC']]
    prev_pcs_normalized, mean, std = normalize_big_numbers(prev_pcs)
    # overwrite the prev_pc column with normalized values
    data['Prev_PC'] = prev_pcs_normalized
    data['Prev_PC'] = data['Prev_PC'].astype('float64')

    feature_normalization_cache = {}
    feature_normalization_cache['Prev_PC'] = (mean, std)

    # Same treatment for PC - constant within this function's single-PC
    # input (mean == the one value, std == 0), so normalize_big_numbers'
    # std==0 guard makes every row 0.0 here. Genuinely inert as a split
    # candidate (see SPATIAL_COLS/POINTER_COLS comment above) but still
    # needs numeric conversion since pystreed can't fit on a raw hex string
    # column.
    pcs = [hex_to_int(pc) for pc in data['PC']]
    pcs_normalized, pc_mean, pc_std = normalize_big_numbers(pcs)
    data['PC'] = pcs_normalized
    data['PC'] = data['PC'].astype('float64')
    feature_normalization_cache['PC'] = (pc_mean, pc_std)

    if data['Type'].iloc[0] == 0:
        INPUT_COLS = SPATIAL_COLS
    else:
        INPUT_COLS = POINTER_COLS

    if 'Delta' in data.columns:
        data['Delta'] = (data['Delta'] / 4096).astype('int')
    if 'Prev_delta' in data.columns:
        data['Prev_delta'] = (data['Prev_delta'] / 4096).astype('int')

    return data[INPUT_COLS], data[OUTPUT_COL], feature_normalization_cache


def load_and_filter_data(filename: str, top_n: int = 0, min_support: int = 0) -> List[dict]:
    """
    Load dataset and filter by PC counts.

    Args:
        filename: Path to the dataset file
        top_n: Number of top PCs to include in the filtered dataset

    Returns:
        List of filtered PC datasets
    """

    if not os.path.exists(filename):
        raise FileNotFoundError(f"Dataset file not found: {filename}")

    # Load the dataset
    data = pd.read_csv(filename)
    data[OUTPUT_COL] = (data[OUTPUT_COL] == 'green').astype(int)

    spatial_data = data[data['Type'] == 0]
    pointer_data = data[data['Type'] == 1]

    def filter_pcs(subset_data):
        # Filter PCs by count threshold - computed within this category only
        # (spatial and pointer counted separately), matching the ensemble
        # pipeline's per-Type keep_pcs convention (gen_tree_code.py). A PC's
        # combined spatial+pointer hit count can clear min_support even when
        # neither category alone has enough real signal to trust on its own
        # (e.g. 60 spatial + 60 pointer hits passing a combined >=100 bar)
        # - per-category filtering avoids training/deploying a tree on that
        # kind of thin, category-specific sample.
        if min_support > 0:
            green_counts = subset_data.loc[subset_data[OUTPUT_COL] == 1, 'PC'].value_counts()
            return green_counts[green_counts >= min_support].sort_values(ascending=False)
        elif top_n > 0:
            return subset_data['PC'].value_counts().sort_values(ascending=False)[:top_n]
        else:
            raise ValueError("Either top_n or min_support must be greater than 0.")

    spatial_pc_counts = filter_pcs(spatial_data)
    pointer_pc_counts = filter_pcs(pointer_data)

    spatial_datasets = []
    # Create separate datasets for each PC
    for pc in spatial_pc_counts.index:
        pc_data = spatial_data[spatial_data['PC'] == pc]
        if pc_data.empty:
            continue
        pc_data = pc_data.reset_index(drop=True)
        X, y, feature_normalization_cache = process_pc_data(pc_data)
        data_object = {
            'pc': pc,
            'X': X,
            'y': y,
            'feature_normalization_cache': feature_normalization_cache
        }
        spatial_datasets.append(data_object)

    pointer_datasets = []
    for pc in pointer_pc_counts.index:
        pc_data = pointer_data[pointer_data['PC'] == pc]
        if pc_data.empty:
            continue
        pc_data = pc_data.reset_index(drop=True)
        X, y, feature_normalization_cache = process_pc_data(pc_data)
        data_object = {
            'pc': pc,
            'X': X,
            'y': y,
            'feature_normalization_cache': feature_normalization_cache
        }
        pointer_datasets.append(data_object)

    return spatial_datasets, pointer_datasets
