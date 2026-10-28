from pystreed import STreeDClassifier


FEATURE_INDEX = {'PC': 0, 'Offset': 1, 'Delta': 2, 'Offset_from_faulting': 3,
                  'Prev_PC': 4, 'Prev_delta': 5}

# Features holding instruction addresses. Their splits are emitted as exact
# sets of 64-bit addresses (see walk_tree()), never as numeric thresholds.
ADDRESS_FEATURES = ('PC', 'Prev_PC')


def _parse_split(name: str):
    """Split a pystreed binarized feature name into (feature, op, operand):
    "F <= 1.5" -> ('F', '<=', 1.5), "F == 2.0" -> ('F', '==', [2.0]),
    "F in [1.0,2.0]" -> ('F', 'in', [1.0, 2.0]) (see pystreed's
    Binarizer.fit())."""
    if " <= " in name:
        feat, thr = name.split(" <= ")
        return feat, '<=', float(thr)
    if " == " in name:
        feat, val = name.split(" == ")
        return feat, 'in', [float(val.strip("'"))]
    if " in [" in name:
        feat, vals = name.split(" in [")
        return feat, 'in', [float(v.strip("'")) for v in vals.rstrip("]").split(",")]
    raise ValueError(f"unrecognized split '{name}'")


def _address_split_sets(feature_name: str, op: str, operand, feature_normalization_cache: dict):
    """The exact training addresses on each side of a split on an address
    feature. Training saw each address z-score normalized per PC dataset
    (preprocessing.py's normalize_big_numbers()); redoing that arithmetic
    here on each distinct raw address reproduces exactly which addresses
    satisfied the split (go left in the C tree) and which didn't."""
    mean, std = feature_normalization_cache[feature_name]
    values = feature_normalization_cache[f"{feature_name}_values"]
    left, right = [], []
    for v in values:
        nv = (v - mean) / std
        sat = (nv <= operand) if op == '<=' else (nv in operand)
        (left if sat else right).append(v)
    return left, right


def walk_tree(tree: STreeDClassifier, feature_normalization_cache: dict) -> list:
    """Flatten a fitted tree into the C node format (see c_header()).

    Each node is (feature, threshold, left, right, leaf_value, left_set,
    right_set). Numeric features split on `value <= threshold` (a double).
    Address features (PC, Prev_PC) split on exact 64-bit sets instead: an
    address in left_set goes left, one in right_set goes right, and one in
    neither (never seen in this PC's training data) predicts "don't
    prefetch" - see native_eval_tree(). A threshold on 47-bit addresses can't
    be kept exactly in a float, and can't be remapped to the deployed
    library layout when its two sides lie in different libraries; each set
    entry is a plain hex address the build-time remap shifts on its own.
    """
    input_features: list[str] = list(tree.feature_names_in_)

    # Node IDs are assigned by append order (reserve a slot, then recurse
    # into children, which append further down the list and report back
    # their own assigned IDs) - NOT by an id+1/id+2 formula. That formula
    # only produces non-colliding indices for a perfectly balanced binary
    # tree of uniform depth; STreeD trees are generally unbalanced (leaves
    # at different depths), so id+1/id+2 for two different branches can
    # land on the same slot and silently clobber each other.
    node_list: list = []

    def walk(node) -> int:
        node_id = len(node_list)
        node_list.append(None)  # reserve the slot before recursing
        if node.is_leaf_node():
            pred = tree._get_label_str(node.label)
            node_list[node_id] = (-1, 0.0, 0, 0, pred, None, None)
        else:
            feature_name, op, operand = _parse_split(input_features[node.feature])
            feat = FEATURE_INDEX[feature_name]
            threshold, left_set, right_set = 0.0, None, None
            if feature_name in ADDRESS_FEATURES:
                left_set, right_set = _address_split_sets(feature_name, op, operand,
                                                          feature_normalization_cache)
            elif op == '<=':
                threshold = operand
            else:
                # "== v" / "in [...]" on a numeric feature: not representable
                # as a single threshold; the caller skips this PC's tree
                raise ValueError(f"unsupported split on numeric feature: {input_features[node.feature]}")
            # PySTreeD's own node.left_child/right_child are the reverse of
            # what their names suggest: right_child is the "predicate
            # satisfied" (feature <= threshold) branch and left_child is
            # "not satisfied" (see base.py's _recursive_print_tree /
            # _recursive_export_dot, which print left_child under "not
            # satisfied" and right_child under "satisfied"). The C node's
            # 'left' slot is taken when the predicate holds, so it must get
            # the *satisfied* branch, i.e. right_child.
            left_id = walk(node.right_child)
            right_id = walk(node.left_child)
            node_list[node_id] = (feat, threshold, left_id, right_id, 0.0, left_set, right_set)
        return node_id

    walk(tree.get_tree())

    return node_list

def fmt_float(v):
    s = "%.9g" % v
    if '.' not in s and 'e' not in s and 'inf' not in s and 'nan' not in s:
        s += ".0"
    return s + "f"


def fmt_double(v):
    return repr(float(v))


# Integer-bounds mode (ST_INTEGER_BOUNDS=1 in the environment, set by
# gen_single_tree_code.py --integer_bounds): every feature the boxes test is
# an integer (slot offsets, page counts), so a bound lo < x <= hi can be
# emitted as integers - floor(lo) < x <= floor(hi) gives the same answer for
# every integer x - and the header then needs no floating point at all (for
# the kernel module, which can't use the FPU in fault context).
import math
import os


def integer_bounds() -> bool:
    return os.environ.get("ST_INTEGER_BOUNDS", "0") == "1"


_I64_MIN, _I64_MAX = -(1 << 63), (1 << 63) - 1


def fmt_bound(v) -> str:
    """A box bound in the header's st_val_t: a double, or (integer-bounds
    mode) the integer floor(v), with the unbounded stand-ins mapped to the
    int64 range."""
    if not integer_bounds():
        return fmt_double(v)
    if v <= -_UNBOUNDED:
        return "ST_VAL_MIN"
    if v >= _UNBOUNDED:
        return "ST_VAL_MAX"
    return f"{math.floor(v)}LL"


# candidate-level numeric features (differ per candidate of one fault) and
# fault-level ones (same for every candidate of a fault)
CANDIDATE_FEATURES = ('Offset', 'Offset_from_faulting', 'Delta')
FAULT_NUMERIC_FEATURES = ('Prev_delta',)
_UNBOUNDED = 1e300   # stand-in for +/-infinity in the generated intervals


def tree_to_boxes(node_list: list) -> list:
    """The tree's positive leaves as boxes: each is the conjunction of the
    conditions on its root-to-leaf path, as
        {'addr': {feature_index: frozenset(allowed addresses)},
         'num': {feature_index: (lo, hi)}}       # lo < value <= hi
    A candidate is predicted positive iff it lies in some box. Address
    conditions only ever admit training addresses (the intersection of the
    path's sets), so an address the tree never saw fails every box whose
    path tests it - the same "don't prefetch" native_eval_tree() returns."""
    boxes = []

    def rec(i, addr, num):
        feature, threshold, left, right, leaf, lset, rset = node_list[i]
        if feature < 0:
            if int(float(leaf)) == 1:
                boxes.append({'addr': dict(addr), 'num': dict(num)})
            return
        if lset is not None or rset is not None:
            prev = addr.get(feature)
            for child, allowed in ((left, frozenset(lset or [])), (right, frozenset(rset or []))):
                a = dict(addr)
                a[feature] = allowed if prev is None else (prev & allowed)
                rec(child, a, num)
        else:
            lo, hi = num.get(feature, (-_UNBOUNDED, _UNBOUNDED))
            rec(left, addr, {**num, feature: (lo, min(hi, threshold))})
            rec(right, addr, {**num, feature: (max(lo, threshold), hi)})

    rec(0, {}, {})
    if len(boxes) > 32:
        raise ValueError(f"{len(boxes)} positive leaves - more than the 32-bit active-box mask holds")
    return boxes


def _box_fault_c(category: str, pc: int, boxes: list) -> str:
    """Per-fault function: which boxes can match at all, given the fault's
    PC, Prev_PC and Prev_delta (the same for all of its candidates)."""
    fname = f"st_{category}_{pc:x}_fault"
    lines = [f"static uint32_t {fname}(uint64_t pc, uint64_t prev_pc, st_val_t prev_delta)", "{"]
    lines.append("    (void)pc; (void)prev_pc; (void)prev_delta;")
    for fi, name in ((FEATURE_INDEX['PC'], 'pc'), (FEATURE_INDEX['Prev_PC'], 'prev_pc')):
        constrained = [b for b in range(len(boxes)) if fi in boxes[b]['addr']]
        free = [b for b in range(len(boxes)) if fi not in boxes[b]['addr']]
        if not constrained:
            continue
        var = f"m_{name}"
        lines.append(f"    uint32_t {var} = 0x{sum(1 << b for b in free):x}u;  /* boxes not testing {name} */")
        by_addr = {}
        for b in constrained:
            for a in boxes[b]['addr'][fi]:
                by_addr[a] = by_addr.get(a, 0) | (1 << b)
        if by_addr:
            lines.append(f"    switch ({name}) {{")
            for a in sorted(by_addr):
                lines.append(f"    case 0x{a:x}ULL: {var} |= 0x{by_addr[a]:x}u; break;")
            lines.append("    default: break;")
            lines.append("    }")
    mask = (1 << len(boxes)) - 1
    lines.append(f"    uint32_t m = 0x{mask:x}u;")
    for name in ('pc', 'prev_pc'):
        if f"uint32_t m_{name} " in "\n".join(lines):
            lines.append(f"    m &= m_{name};")
    pdi = FEATURE_INDEX['Prev_delta']
    for b, box in enumerate(boxes):
        if pdi in box['num']:
            lo, hi = box['num'][pdi]
            lines.append(f"    if (!(prev_delta > {fmt_bound(lo)} && prev_delta <= {fmt_bound(hi)})) m &= ~(1u << {b});")
    lines.append("    return m;")
    lines.append("}")
    return "\n".join(lines) + "\n"


def convert_tree_to_c_code(pc: int, tree: STreeDClassifier, feature_normalization_cache: dict, spatial_or_pointer="") -> str:
    """One PC's tree as (1) its positive-leaf boxes' candidate-level ranges
    and (2) a compiled per-fault function picking the boxes that apply to a
    fault (see tree_to_boxes() and native_tree_eval.h)."""
    node_list = walk_tree(tree, feature_normalization_cache)
    boxes = tree_to_boxes(node_list)
    cat = spatial_or_pointer
    c_code = f"/* PC 0x{pc:x}: {len(node_list)}-node tree, {len(boxes)} positive leaves */\n"
    if boxes:
        c_code += _box_fault_c(cat, pc, boxes)
        c_code += f"static const st_box_t st_{cat}_{pc:x}_boxes[{len(boxes)}] = {{\n"
        for box in boxes:
            rng = []
            for f in CANDIDATE_FEATURES:
                lo, hi = box['num'].get(FEATURE_INDEX[f], (-_UNBOUNDED, _UNBOUNDED))
                rng += [fmt_bound(lo), fmt_bound(hi)]
            c_code += "    {" + ", ".join(rng) + "},\n"
        c_code += "};\n"
    c_code += "\n"
    return c_code


def denormalize_feat_rows(X, feature_normalization_cache: dict) -> list:
    """Reconstruct the *raw* feature vectors the C tree is evaluated on from
    X (which has PC/Prev_PC z-score normalized - see preprocessing.py's
    process_pc_data()). Address features come back as exact ints, by looking
    the normalized value up among the dataset's distinct raw addresses
    (inverting the arithmetic in floating point would round 47-bit
    addresses); every other feature passes through as a float. Used by
    main.py's training workers to hand back plain, picklable feature rows
    for test_c_parity.py.
    """
    norm_to_raw = {}
    for col in ADDRESS_FEATURES:
        if f"{col}_values" in feature_normalization_cache:
            mean, std = feature_normalization_cache[col]
            norm_to_raw[col] = {(v - mean) / std: v for v in feature_normalization_cache[f"{col}_values"]}
    feat_rows = []
    for _, row in X.iterrows():
        feat = [0.0] * len(FEATURE_INDEX)
        for col, idx in FEATURE_INDEX.items():
            if col not in X.columns:
                continue
            v = row[col]
            if col in norm_to_raw:
                feat[idx] = int(norm_to_raw[col][v])
            else:
                feat[idx] = float(v)
        feat_rows.append(feat)
    return feat_rows


def _pc_to_int(pc) -> int:
    """PC values are hex strings without a '0x' prefix (e.g. '7ffff57922ed')."""
    return int(pc, 16) if isinstance(pc, str) else int(pc)


def convert_models_to_c_code(pc_to_model_map: dict, feature_normalization_cache_map: dict) -> str:
    """Convert a {'spatial'|'pointer': {pc: STreeDClassifier}} map (with the
    matching per-PC feature_normalization_cache_map) to one header. Call
    once per category - see assemble_header_from_c_fragments()."""
    entries = {}
    category = None
    for cat in ('spatial', 'pointer'):
        for pc, model in pc_to_model_map.get(cat, {}).items():
            category = cat
            norm_cache = feature_normalization_cache_map.get(cat, {}).get(pc, {})
            pc_int = _pc_to_int(pc)
            try:
                code = convert_tree_to_c_code(pc_int, model, norm_cache, cat)
            except ValueError as e:
                print(f"skipping PC 0x{pc_int:x} ({cat}): unsupported split ({e})")
                continue
            entries[pc] = (code, len(tree_to_boxes(walk_tree(model, norm_cache))))
    return assemble_header_from_c_fragments(entries, category) if entries else empty_header(category or "spatial")


def _router_c(category: str, pcs_with_counts: list) -> str:
    """st_<category>_lookup(pc, &model): the PC's per-fault function and box
    table, or 0 when the PC has no tree that can ever predict positive."""
    lines = [f"static inline int st_{category}_lookup(uint64_t pc, st_model_t *model)", "{",
             "    switch (pc) {"]
    for pc_int, nboxes in pcs_with_counts:
        if nboxes == 0:
            continue
        lines += [f"    case 0x{pc_int:x}:",
                  f"        model->fault = st_{category}_{pc_int:x}_fault;",
                  f"        model->boxes = st_{category}_{pc_int:x}_boxes;",
                  f"        model->nboxes = {nboxes};",
                  "        return 1;"]
    lines += ["    default:", "        return 0;", "    }", "}"]
    return "\n".join(lines) + "\n"


def assemble_header_from_c_fragments(pc_code_map: dict, category: str) -> str:
    """Assemble a header from already-generated per-PC C code fragments.

    pc_code_map: {pc: (c_code_str, nboxes)} for the given category - each
    fragment was produced by convert_tree_to_c_code(pc, ..., category)
    already (see main.py's _fit_and_convert_worker(), which runs
    fit()+convert_tree_to_c_code() together inside a subprocess so a
    pystreed native crash on one PC can't take down the whole training
    run). This concatenates the fragments and adds the st_<category>_lookup()
    router.

    Call once per category: combining spatial and pointer PCs in one call
    could emit duplicate `case 0x...:` labels if the same PC appears in both.
    """
    c_code = c_header()
    for pc, (fragment, _nboxes) in pc_code_map.items():
        c_code += fragment
    c_code += _router_c(category, [(_pc_to_int(pc), nboxes) for pc, (_f, nboxes) in pc_code_map.items()])
    return c_code


def c_header():
    header = ""
    header += "#ifdef __KERNEL__\n#include <linux/types.h>\n#else\n"
    header += "#include <stdint.h>\n#include <stddef.h>\n#endif\n\n"
    header += "/* tree format 3: each PC's tree as its positive-leaf boxes - a compiled\n"
    header += " * per-fault function (PC/Prev_PC/Prev_delta -> active boxes) plus the\n"
    header += " * boxes' candidate-level ranges (lo < value <= hi on Offset,\n"
    header += " * Offset_from_faulting, Delta). See native_tree_eval.h. */\n"
    header += "#define NATIVE_TREE_FORMAT 3\n"
    header += "#ifndef ST_MODEL_TYPES\n#define ST_MODEL_TYPES\n"
    if integer_bounds():
        header += "/* integer bounds: no floating point (usable in the kernel) */\n"
        header += "#define ST_INTEGER_BOUNDS 1\n"
        header += "typedef int64_t st_val_t;\n"
        header += "#define ST_VAL_MIN ((st_val_t)(-9223372036854775807LL - 1))\n"
        header += "#define ST_VAL_MAX ((st_val_t)9223372036854775807LL)\n"
    else:
        header += "typedef double st_val_t;\n"
    header += "typedef struct {\n"
    header += "    st_val_t off_lo, off_hi;    /* Offset (pointer slot index; -1 for spatial) */\n"
    header += "    st_val_t ofl_lo, ofl_hi;    /* Offset_from_faulting */\n"
    header += "    st_val_t d_lo, d_hi;        /* Delta, in pages */\n"
    header += "} st_box_t;\n"
    header += "typedef struct {\n"
    header += "    uint32_t (*fault)(uint64_t pc, uint64_t prev_pc, st_val_t prev_delta);\n"
    header += "    const st_box_t *boxes;\n"
    header += "    int nboxes;\n"
    header += "} st_model_t;\n"
    header += "#endif\n\n"
    return header


def empty_header(category: str) -> str:
    """A valid header for a category with no trained trees: every PC gets
    "no model", so the runtime skips that stage for every fault."""
    return (c_header() + f"/* no {category} trees were trained (no PC reached min_support) */\n"
            f"static inline int st_{category}_lookup(uint64_t pc, st_model_t *model)\n"
            "{\n    (void)pc; (void)model;\n    return 0;\n}\n")
