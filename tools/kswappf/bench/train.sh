#!/bin/bash
# Label the brd trace and train PySTreeD F0.5 spatial trees (depth 3, 5 nodes, min support
# 100, integer bounds), then build the module. Usage: train.sh <wl> <N> <claim 1|0>
# Output: $L/eden_swappf_n<N>_c<claim>.ko (load with next_n=N).
. /data1/shaurya/linux66_bench/common.sh
WL=$1; N=$2; C=$3; . $B/wl/$WL.sh; L=$WL_DIR/linux66b; tag=n${N}_c$C
E=/home/shaurya/eden/tools/fltrace
RAW=$L/raw.out
# WL_TRAIN_FAULTS: train on the first that many faults only (traces too large to label whole)
if [ -n "${WL_TRAIN_FAULTS:-}" ]; then
    RAW=$L/raw_first$WL_TRAIN_FAULTS.out
    flock $L/.raw.lock sh -c "[ -s $RAW ] || head -n $WL_TRAIN_FAULTS $L/raw.out > $RAW"
fi
cd /home/shaurya/pointer_analysis
PF_SPATIAL_N=$N PF_SPATIAL_CLAIM=$C python3 deku/trace_processing/process_linux_trace.py $RAW > $L/processed_$tag.csv 2> $L/process_$tag.log
cd $L
python3 - $tag <<'PY'
import sys, pandas as pd
t = sys.argv[1]
d = pd.read_csv(f"processed_{t}.csv", dtype={"PC": str, "Prev_PC": str})
print(t, len(d), "rows;", d["Color"].value_counts().to_dict())
st = d.drop(columns=["Delta"]).rename(columns={"Cand_delta": "Delta"})
st[["PC","Prev_PC","Index","Color","Offset_from_faulting","Offset","Delta","Prev_delta","Distance","Type"]].to_csv(f"single_tree_{t}.csv", index=False)
PY
rm -f processed_$tag.csv
mkdir -p headers_$tag
cd $E/single_tree_codegen
python3 gen_single_tree_code.py $L/single_tree_$tag.csv --min_support 100 --max_depth 3 --max_num_nodes 5 \
    --f_beta 0.5 --integer_bounds --out_dir $L/headers_$tag > $L/train_$tag.log 2>&1
grep "native_tree_model_spatial" $L/train_$tag.log
cd /home/shaurya/eden/tools/kswappf
flock /data1/shaurya/swappf_build.lock sh -c "make clean >/dev/null 2>&1; make TREES=$L/headers_$tag 2>&1 | grep -E 'error|warning'; cp eden_swappf.ko $L/eden_swappf_$tag.ko; make clean >/dev/null 2>&1"
ls $L/eden_swappf_$tag.ko
