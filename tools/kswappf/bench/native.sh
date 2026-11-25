#!/bin/bash
# Native reference run of one workload (no memory limit, no swap pressure): reference
# outputs for validation and memory.peak for sizing the cgroup. Usage: native.sh <wl>
. /data1/shaurya/linux66_bench/common.sh
load_wl $1
check_hygiene
sudo rmmod eden_swappf 2>/dev/null || true
W=$L/native; rm -rf $W; mkdir -p $W; drop_caches
echo max | sudo tee $CG/memory.max >/dev/null
type wl_prepare >/dev/null 2>&1 && wl_prepare $W
MODE_NATIVE=1 MODE_TRACE=0 RUN_MODE=0
sudo rmdir $CG 2>/dev/null || true; sudo mkdir -p $CG    # fresh memory.peak
# sample the anonymous-memory peak too: memory.peak includes file cache (e.g. a graph file read at startup)
( a=0; while [ -d $CG ]; do x=$(awk '$1=="anon" {print $2}' $CG/memory.stat 2>/dev/null || echo 0); [ "${x:-0}" -gt $a ] && a=$x && echo $a > $W/anon_peak; sleep 0.2; done ) &
anon_sampler=$!
wl_run $W
kill $anon_sampler 2>/dev/null || true
cat $CG/memory.peak > $W/memory_peak
echo "native: metric=$(cat $W/metric) valid=$(cat $W/valid) peak_bytes=$(cat $W/memory_peak) anon_peak_bytes=$(cat $W/anon_peak 2>/dev/null)"
drop_caches
