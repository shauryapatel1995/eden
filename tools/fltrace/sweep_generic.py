import sys
import pandas as pd
import numpy as np
import xgboost as xgb
from sklearn.model_selection import train_test_split

# Dedicated per-type (spatial or pointer) denom sweep - each candidate type
# can have a wildly different class-imbalance ratio depending on the
# workload (e.g. mcf pointer ~1400:1 vs redis pointer ~28:1), so a denom
# tuned for one type/workload can catastrophically under- or over-fire on
# another. Sweeps target scale_pos_weight values directly, deriving denom
# from each dataset's own measured imbalance ratio.

if len(sys.argv) < 5:
    print("Usage: sweep_generic.py <input_csv> <model_output_dir> <min_hits_per_pc> <type_val 0=spatial|1=pointer> [n_estimators]")
    sys.exit(1)

input_file = sys.argv[1]
model_output_dir = sys.argv[2]
min_hits_per_pc = int(sys.argv[3])
type_val = int(sys.argv[4])
type_name = 'spatial' if type_val == 0 else 'pointer'
n_estimators = int(sys.argv[5]) if len(sys.argv) > 5 else 50

import os
os.makedirs(model_output_dir, exist_ok=True)

cols = ['PC', 'Offset', 'Cand_delta', 'Offset_from_faulting', 'Color', 'Type',
        'Prev_PC', 'Prev_delta']
print(f"Loading {input_file}...", flush=True)
data = pd.read_csv(input_file, usecols=cols)
print(f"Loaded {len(data):,} rows", flush=True)

before = len(data)
data = data[data['Type'] == type_val].reset_index(drop=True)
print(f"Filtered to {type_name} (Type={type_val}): {len(data):,} rows (dropped {before - len(data):,} other-type rows)", flush=True)

hex_re = r'^[0-9a-fA-F]+$'
int_re = r'^-?[0-9]+$'
valid_mask = (
    data['PC'].astype(str).str.match(hex_re) &
    data['Prev_PC'].astype(str).str.match(hex_re) &
    data['Cand_delta'].astype(str).str.match(int_re) &
    data['Prev_delta'].astype(str).str.match(int_re) &
    data['Offset_from_faulting'].astype(str).str.match(int_re) &
    data['Offset'].astype(str).str.match(int_re) &
    data['Color'].isin(['red', 'green'])
)
before = len(data)
data = data[valid_mask].reset_index(drop=True)
print(f"Dropped {before - len(data):,} corrupted rows, {len(data):,} remain", flush=True)

hit_counts = data.loc[data['Color'] == 'green', 'PC'].value_counts()
keep_pcs = set(hit_counts[hit_counts >= min_hits_per_pc].index)
print(f"{len(hit_counts):,} distinct PCs have >=1 cache hit; "
      f"{len(keep_pcs):,} have >= {min_hits_per_pc} cache hits", flush=True)

before = len(data)
data = data[data['PC'].isin(keep_pcs)].reset_index(drop=True)
print(f"After PC filter: {len(data):,} rows remain "
      f"(dropped {before - len(data):,}, {(before - len(data))/before*100:.1f}%)", flush=True)

data['PC'] = data['PC'].apply(lambda x: int(str(x), 16)).astype("float64")
data['Prev_PC'] = data['Prev_PC'].apply(lambda x: int(str(x), 16)).astype("float64")
data['Color'] = data['Color'].map({'red': 0, 'green': 1})
data['Cand_delta'] = data['Cand_delta'].apply(lambda x: int(str(x)))
data['Cand_delta'] = (data['Cand_delta'] / 4096).astype("float64")
data['Prev_delta'] = data['Prev_delta'].apply(lambda x: int(str(x)))
data['Prev_delta'] = (data['Prev_delta'] / 4096).astype("float64")
data['Offset'] = data['Offset'].astype("float64")
data['Offset_from_faulting'] = data['Offset_from_faulting'].astype("float64")

feature_cols = ['PC', 'Offset', 'Cand_delta', 'Offset_from_faulting', 'Prev_PC', 'Prev_delta']

cache_hits = (data['Color'] == 1).sum()
mispredictions = (data['Color'] == 0).sum()
imbalance = mispredictions / cache_hits
print(f"{type_name}: {len(data):,} rows, {cache_hits:,} hits, {mispredictions:,} misses, imbalance={imbalance:.2f}:1", flush=True)

X = data[feature_cols]
y = data['Color']
train_idx, test_idx = train_test_split(data.index, test_size=0.5, random_state=963)
X_train, y_train = X.loc[train_idx], y.loc[train_idx]
X_test, y_test = X.loc[test_idx], y.loc[test_idx]
y_test_arr = y_test.to_numpy()

target_pos_weights = [0.3, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 5.0]

best = None
print(f"\n{'denom':>10} {'pos_weight':>10} {'precision':>10} {'recall':>10} {'f1':>8} {'f0.5':>8}", flush=True)
for pw in target_pos_weights:
    denom = imbalance / pw
    model = xgb.XGBClassifier(objective='binary:logistic', n_estimators=n_estimators, max_depth=6,
                               scale_pos_weight=pw, seed=123)
    model.fit(X_train, y_train)
    probs = model.predict_proba(X_test)[:, 1]
    preds = (probs >= 0.5).astype(int)
    tp = ((preds == 1) & (y_test_arr == 1)).sum()
    fp = ((preds == 1) & (y_test_arr == 0)).sum()
    fn = ((preds == 0) & (y_test_arr == 1)).sum()
    precision = tp / (tp + fp) if (tp + fp) > 0 else 0
    recall = tp / (tp + fn) if (tp + fn) > 0 else 0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) > 0 else 0
    beta2 = 0.25
    f05 = (1 + beta2) * precision * recall / (beta2 * precision + recall) if (precision + recall) > 0 else 0
    print(f"{denom:10.2f} {pw:10.4f} {precision:10.4f} {recall:10.4f} {f1:8.4f} {f05:8.4f}", flush=True)
    if best is None or f05 > best['f05']:
        best = {'pw': pw, 'denom': denom, 'precision': precision, 'recall': recall,
                'f1': f1, 'f05': f05, 'model': model}

print(f"\nBest by F0.5: pos_weight={best['pw']:.4f} denom={best['denom']:.2f} "
      f"precision={best['precision']:.4f} recall={best['recall']:.4f} f0.5={best['f05']:.4f}", flush=True)
model_path = os.path.join(model_output_dir, f"mcf-test-trace_{type_name}_filtered.json")
best['model'].save_model(model_path)
print(f"Saved best {type_name} model to {model_path}", flush=True)
