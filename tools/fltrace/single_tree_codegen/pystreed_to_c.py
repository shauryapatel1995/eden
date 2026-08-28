from pystreed import STreeDClassifier


FEATURE_INDEX = {'PC': 0, 'Offset': 1, 'Delta': 2, 'Offset_from_faulting': 3,
                  'Prev_PC': 4, 'Prev_delta': 5}

def walk_tree(tree: STreeDClassifier, feature_normalization_cache: dict) -> list:
    """Convert a decision tree to C code for prediction.

    The function header for the C code should be
    int predict_single(global_model, features)

    where global model is a pointer to a struct representing the model,
    features is a pointer to an array of feature values of type float.
    The function should return 1 if the prediction is positive
    (i.e., the predicted class is 1) and 0 otherwise.
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
            node_list[node_id] = (-1, 0.0, 0, 0, 0, pred)
        else:
            feature_name, threshold = input_features[node.feature].split(" <= ")
            threshold = float(threshold)
            if feature_name in feature_normalization_cache:
                mean, std = feature_normalization_cache[feature_name]
                # Adjust threshold based on normalization
                threshold = threshold * std + mean
                if threshold < 0:
                    threshold -= 1

            feat = FEATURE_INDEX[feature_name]
            # PySTreeD's own node.left_child/right_child are the reverse of
            # what their names suggest: right_child is the "predicate
            # satisfied" (feature <= threshold) branch and left_child is
            # "not satisfied" (see base.py's _recursive_print_tree /
            # _recursive_export_dot, which print left_child under "not
            # satisfied" and right_child under "satisfied"). The C node's
            # 'left' slot is walked by native_eval_tree when v <= threshold,
            # so it must get the *satisfied* branch, i.e. right_child.
            left_id = walk(node.right_child)
            right_id = walk(node.left_child)
            node_list[node_id] = (feat, threshold, left_id, right_id, 1, 0.0)
        return node_id

    walk(tree.get_tree())

    return node_list

def fmt_float(v):
    s = "%.9g" % v
    if '.' not in s and 'e' not in s and 'inf' not in s and 'nan' not in s:
        s += ".0"
    return s + "f"

def node_to_c(node: tuple) -> str:
    feature, threshold, left, right, missing_left, leaf_value = node
    return f"{{{feature}, {fmt_float(threshold)}, {left}, {right}, {missing_left}, {leaf_value}}}"

def convert_tree_to_c_code(pc: int, tree: STreeDClassifier, feature_normalization_cache: dict, spatial_or_pointer="") -> str:
    node_list = walk_tree(tree, feature_normalization_cache)
    c_code = f"static const native_tree_node_t native_nodes_{spatial_or_pointer}_{pc:x}[{len(node_list)}] = {{\n"
    for node in node_list:
        c_code += "    " + node_to_c(node) + ",\n"
    c_code += "};\n\n"

    return c_code


def denormalize_feat_rows(X, feature_normalization_cache: dict) -> list:
    """Reconstruct the *raw* feature vectors native_eval_tree expects from X
    (which has PC/Prev_PC z-score normalized - see preprocessing.py's
    process_pc_data()), by inverting that normalization with the per-feature
    (mean, std) pairs in feature_normalization_cache. Every other feature
    passes through unchanged. Used both by main.py's training workers (to
    hand back plain, picklable feature rows for the parity test, since the
    fitted model object itself can't cross the subprocess boundary - see
    _fit_and_convert_worker's docstring) and by test_c_parity.py (to build
    the C test harness's input rows).
    """
    feat_rows = []
    for _, row in X.iterrows():
        feat = [0.0] * len(FEATURE_INDEX)
        for col, idx in FEATURE_INDEX.items():
            if col not in X.columns:
                continue
            v = row[col]
            if col in feature_normalization_cache:
                mean, std = feature_normalization_cache[col]
                v = v * std + mean
            feat[idx] = float(v)
        feat_rows.append(feat)
    return feat_rows


def _pc_to_int(pc) -> int:
    """PC values are hex strings without a '0x' prefix (e.g. '7ffff57922ed')."""
    return int(pc, 16) if isinstance(pc, str) else int(pc)


def convert_models_to_c_code(pc_to_model_map: dict, feature_normalization_cache_map: dict) -> str:
    """Convert the PC -> model map produced by main.py's main() to C code.

    pc_to_model_map has the shape returned by main() in main.py:
        {
            'spatial': {pc: STreeDClassifier, ...},
            'pointer': {pc: STreeDClassifier, ...},
        }

    feature_normalization_cache_map has the matching per-PC shape (the
    second value main() returns):
        {
            'spatial': {pc: feature_normalization_cache, ...},
            'pointer': {pc: feature_normalization_cache, ...},
        }
    (each PC's dataset has its own Prev_PC mean/std, so the cache can't be
    shared across PCs the way a single dict would imply.)

    Generates one native_nodes_<category>_<pc>[] node array per PC (see
    convert_tree_to_c_code) plus a routing function, get_native_nodes, that
    returns the node array (and its length) for a given PC, or NULL if the
    PC has no trained tree - the runtime walks whatever get_native_nodes()
    returns with native_eval_tree() (see native_tree_eval.h).

    Call this once per category (pc_to_model_map containing only one of
    'spatial'/'pointer') when generating the header for a given
    single_tree_predict_{spatial,pointer}.c: since spatial and pointer PCs
    are drawn from the same trace and can collide, combining both
    categories into one call could emit two `case 0x...:` labels for the
    same PC value, which is a C compile error.
    """
    header = c_header()
    pc_entries = []
    seen_pcs = set()
    for category in ('spatial', 'pointer'):
        for pc, model in pc_to_model_map.get(category, {}).items():
            pc_int = _pc_to_int(pc)
            if pc_int in seen_pcs:
                raise ValueError(
                    f"PC 0x{pc_int:x} present in more than one category - "
                    "generate spatial and pointer headers with separate "
                    "convert_models_to_c_code() calls to avoid duplicate "
                    "switch cases in get_native_nodes()")
            seen_pcs.add(pc_int)
            norm_cache = feature_normalization_cache_map.get(category, {}).get(pc, {})
            pc_entries.append((pc_int, model, category, norm_cache))

    c_code = ""
    node_counts = {}
    converted_entries = []
    for pc_int, model, category, norm_cache in pc_entries:
        # walk_tree() only understands "<=" (numeric threshold) splits.
        # pystreed's binarizer emits "== value" for a feature that has
        # exactly 2 distinct values in this PC's local training data, and
        # "in [...]" for a handful of categories - both real, expected
        # outputs from pystreed itself (see Binarizer.fit() in
        # pystreed/binarizer.py), not malformed input. The native C tree
        # format/native_eval_tree() only supports <=, so a PC whose tree
        # contains one of these splits can't be represented - skip it
        # (same as a PC with no trained tree at all: get_native_nodes()
        # returns NULL, candidate treated as not prefetchable) rather than
        # crashing the whole codegen run over one PC's degenerate feature
        # distribution.
        try:
            tree_code = convert_tree_to_c_code(pc_int, model, norm_cache, category)
        except ValueError as e:
            print(f"skipping PC 0x{pc_int:x} ({category}): unsupported split ({e})")
            continue
        c_code += tree_code
        node_counts[pc_int] = len(walk_tree(model, {}))
        converted_entries.append((pc_int, model, category, norm_cache))
    pc_entries = converted_entries

    c_code += "static const native_tree_node_t* get_native_nodes(uint64_t pc, int* num_nodes) {\n"
    c_code += "    switch (pc) {\n"
    for pc_int, _, category, _ in pc_entries:
        c_code += f"        case 0x{pc_int:x}:\n"
        c_code += f"            *num_nodes = {node_counts[pc_int]};\n"
        c_code += f"            return native_nodes_{category}_{pc_int:x};\n"
    c_code += "        default:\n"
    c_code += "            *num_nodes = 0;\n"
    c_code += "            return NULL;\n"
    c_code += "    }\n"
    c_code += "}\n"

    c_code = header + c_code

    return c_code

def assemble_header_from_c_fragments(pc_code_map: dict, category: str) -> str:
    """Assemble a header from already-generated per-PC C code fragments.

    pc_code_map: {pc: (c_code_str, num_nodes)} for the given category - each
    fragment was produced by convert_tree_to_c_code(pc, ..., category)
    already (see main.py's _fit_and_convert_worker(), which runs
    fit()+convert_tree_to_c_code() together inside a subprocess so a
    pystreed native crash on one PC can't take down the whole training
    run). This just concatenates the fragments and adds the
    get_native_nodes() router, mirroring convert_models_to_c_code() but
    skipping tree-walking work that's already done.

    Call once per category, same reasoning as convert_models_to_c_code():
    combining spatial and pointer PCs in one call could emit duplicate
    `case 0x...:` labels if the same PC appears in both.
    """
    c_code = c_header()
    for pc, (fragment, _num_nodes) in pc_code_map.items():
        c_code += fragment

    c_code += "static const native_tree_node_t* get_native_nodes(uint64_t pc, int* num_nodes) {\n"
    c_code += "    switch (pc) {\n"
    for pc, (_fragment, num_nodes) in pc_code_map.items():
        pc_int = _pc_to_int(pc)
        c_code += f"        case 0x{pc_int:x}:\n"
        c_code += f"            *num_nodes = {num_nodes};\n"
        c_code += f"            return native_nodes_{category}_{pc_int:x};\n"
    c_code += "        default:\n"
    c_code += "            *num_nodes = 0;\n"
    c_code += "            return NULL;\n"
    c_code += "    }\n"
    c_code += "}\n"
    return c_code


def c_header():
    header = ""
    # self-contained: don't rely on whatever includes this header having
    # already pulled in stdint.h (uint64_t, used by get_native_nodes) /
    # stddef.h (NULL, used in its default case)
    header += "#include <stdint.h>\n"
    header += "#include <stddef.h>\n\n"
    header += "typedef struct {\n"
    header += "    int feature;       /* -1 if leaf */\n"
    header += "    float threshold;\n"
    header += "    int left;\n"
    header += "    int right;\n"
    header += "    int missing_left;\n"
    header += "    float leaf_value;\n"
    header += "} native_tree_node_t;\n\n"
    return header
