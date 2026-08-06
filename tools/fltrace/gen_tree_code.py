import xgboost as xgb
import json
import sys
import math

MODEL_PATH = sys.argv[1]
OUT_PATH = sys.argv[2]
KEEP_PCS_PATH = sys.argv[3] if len(sys.argv) > 3 else None

FEATURE_INDEX = {'PC': 0, 'Offset': 1, 'Cand_delta': 2, 'Offset_from_faulting': 3,
                  'Prev_PC': 4, 'Prev_delta': 5}

booster = xgb.Booster()
booster.load_model(MODEL_PATH)
dump = booster.get_dump(dump_format='json')
cfg = json.loads(booster.save_config())
base_score_prob = float(cfg['learner']['learner_model_param']['base_score'])
# base_score is stored as a probability (binary:logistic inverse-link); the
# raw tree margins are summed in logit space, so it must be logit-transformed
# before being added in, not used as-is.
base_score = math.log(base_score_prob / (1.0 - base_score_prob))
num_trees = len(dump)
print(f"num_trees={num_trees} base_score={base_score}", file=sys.stderr)

all_nodes = []   # flat list of (feature, threshold, left, right, missing_left, leaf_value, is_leaf)
tree_roots = []   # index into all_nodes for each tree's root
tree_offsets = []
tree_sizes = []

for tree_json in dump:
    root = json.loads(tree_json)
    nodes = {}
    def walk(node):
        nid = node['nodeid']
        if 'leaf' in node:
            nodes[nid] = {'is_leaf': True, 'leaf_value': node['leaf']}
        else:
            feat = FEATURE_INDEX[node['split']]
            thresh = node['split_condition']
            yes = node['yes']
            no = node['no']
            missing = node['missing']
            missing_left = 1 if missing == yes else 0
            nodes[nid] = {'is_leaf': False, 'feature': feat, 'threshold': thresh,
                          'left': yes, 'right': no, 'missing_left': missing_left}
            for c in node['children']:
                walk(c)
    walk(root)

    # renumber nodes 0..N-1 in a stable (BFS-ish, using original nodeid order) fashion,
    # remapping left/right/root references to the local array-index space
    orig_ids = sorted(nodes.keys())
    remap = {orig_id: i for i, orig_id in enumerate(orig_ids)}

    offset = len(all_nodes)
    for orig_id in orig_ids:
        n = nodes[orig_id]
        if n['is_leaf']:
            all_nodes.append((-1, 0.0, 0, 0, 0, n['leaf_value']))
        else:
            all_nodes.append((n['feature'], n['threshold'],
                               offset + remap[n['left']], offset + remap[n['right']],
                               n['missing_left'], 0.0))
    tree_offsets.append(offset)
    tree_sizes.append(len(orig_ids))
    tree_roots.append(offset + remap[0])

def fmt_float(v):
    s = "%.9g" % v
    if '.' not in s and 'e' not in s and 'inf' not in s and 'nan' not in s:
        s += ".0"
    return s + "f"

with open(OUT_PATH, 'w') as f:
    f.write("/* Auto-generated from %s - do not hand-edit.\n" % MODEL_PATH)
    f.write(" * Regenerate with gen_tree_code.py. */\n\n")
    f.write("#define NATIVE_NUM_TREES %d\n" % num_trees)
    f.write("#define NATIVE_NUM_NODES %d\n" % len(all_nodes))
    f.write("#define NATIVE_BASE_MARGIN_PLACEHOLDER 0.0f /* solved empirically, see below */\n\n")
    f.write("typedef struct {\n")
    f.write("    int feature;       /* -1 if leaf */\n")
    f.write("    float threshold;\n")
    f.write("    int left;\n")
    f.write("    int right;\n")
    f.write("    int missing_left;\n")
    f.write("    float leaf_value;\n")
    f.write("} native_tree_node_t;\n\n")

    f.write("static const native_tree_node_t native_nodes[NATIVE_NUM_NODES] = {\n")
    for n in all_nodes:
        feature, threshold, left, right, missing_left, leaf_value = n
        f.write("    {%d, %s, %d, %d, %d, %s},\n" % (
            feature, fmt_float(threshold), left, right, missing_left, fmt_float(leaf_value)))
    f.write("};\n\n")

    f.write("static const int native_tree_roots[NATIVE_NUM_TREES] = {\n    ")
    f.write(", ".join(str(r) for r in tree_roots))
    f.write("\n};\n\n")

    f.write("static const float native_base_score = %s;\n" % fmt_float(base_score))

    if KEEP_PCS_PATH:
        with open(KEEP_PCS_PATH) as kf:
            keep_pcs = sorted(json.load(kf))
        f.write("\n/* PCs the training data actually had >=100 real cache hits for -\n")
        f.write(" * see %s. Candidates from any other PC are extrapolating outside\n" % KEEP_PCS_PATH)
        f.write(" * the training distribution for this feature - skip the full tree\n")
        f.write(" * ensemble for them entirely (native_pc_is_relevant() below, binary\n")
        f.write(" * search since this array is kept sorted). */\n")
        f.write("#define NATIVE_NUM_RELEVANT_PCS %d\n" % len(keep_pcs))
        f.write("static const uint64_t native_relevant_pcs[NATIVE_NUM_RELEVANT_PCS] = {\n    ")
        f.write(", ".join("0x%xULL" % pc for pc in keep_pcs))
        f.write("\n};\n")

print(f"wrote {OUT_PATH}: {len(all_nodes)} nodes across {num_trees} trees" +
      (f", {len(keep_pcs)} relevant PCs" if KEEP_PCS_PATH else ""), file=sys.stderr)
