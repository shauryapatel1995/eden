#!/bin/bash
# Build eden_swappf_corun_<cfg>.ko (current module source) for a workload from its saved tree headers,
# converting format-3 spatial headers to format 5 (extra box fields = MIN/MAX) and an empty stack header.
# Usage: build_corun_ko.sh <wl> <cfg>
set -e
B=/data1/shaurya/linux66_bench; WL=$1; cfg=$2
L=$(bash -c ". $B/wl/$WL.sh; echo \$WL_DIR")/linux66b
H=$L/headers_$cfg; [ -d $H ] || H=$(dirname $(readlink -f $L/eden_swappf_$cfg.ko 2>/dev/null || echo /x/x))/headers_$cfg
[ -f $H/native_tree_model_spatial.h ] || { echo "$WL: no headers for $cfg"; exit 1; }
O=${H}_corun; mkdir -p $O
Wh=/data1/shaurya/deku_wiredtiger/linux66b/headers_n32_c0_corun
S=$H/native_tree_model_spatial.h
if grep -q "NATIVE_TREE_FORMAT 5" $S; then cp $S $O/; else
  diff -q <(head -28 $S) <(head -28 /data1/shaurya/deku_wiredtiger/linux66b/headers_n32_c0/native_tree_model_spatial.h) >/dev/null || { echo "$WL: unexpected header prelude"; exit 1; }
  { head -31 $Wh/native_tree_model_spatial.h; tail -n +29 $S | sed -E 's/^(    \{[^{},]+,[^{},]+,[^{},]+,[^{},]+,[^{},]+,[^{},]+)\},$/\1, ST_VAL_MIN, ST_VAL_MAX, ST_VAL_MIN, ST_VAL_MAX, ST_VAL_MIN, ST_VAL_MAX},/'; } > $O/native_tree_model_spatial.h
  nb=$(grep -c '^    {' $S); nc=$(grep -c 'ST_VAL_MIN, ST_VAL_MAX},$' $O/native_tree_model_spatial.h)
  [ $nb = $nc ] || { echo "$WL: converted $nc of $nb boxes"; exit 1; }
fi
cp $Wh/native_tree_model_stack.h $O/
cd /home/shaurya/eden/tools/kswappf
flock /data1/shaurya/swappf_build.lock sh -c "make clean >/dev/null 2>&1; make TREES=$O 2>&1 | grep -E 'error|warning' ; cp eden_swappf.ko $L/eden_swappf_corun_$cfg.ko; make clean >/dev/null 2>&1" || true
modinfo -p $L/eden_swappf_corun_$cfg.ko | grep -q target_tgid2 && echo "$WL $cfg: built $L/eden_swappf_corun_$cfg.ko from $O"
