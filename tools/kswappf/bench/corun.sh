#!/bin/bash
# Co-run experiment: <copies> (1 or 2) copies of one workload started together, each in its
# own cgroup (memory.max = WL_MEM, as run.sh) and run dir, all swapping to the same SSD swap.
# One module for every config (eden_swappf_corun_n32_c0.ko: the workload's n32_c0 spatial
# trees, no stack trees; target_tgid + target_tgid2 so both copies are acted on).
# Usage: corun.sh <wl> <cfg: noprefetch|default|n32_c0> <round> <copies>
. /data1/shaurya/linux66_bench/common.sh
load_wl $1; cfg=$2; round=$3; NC=$4
D=$L/runs_corun; mkdir -p $D
ko=$L/eden_swappf_corun_n32_c0.ko; n=32
case $cfg in
  noprefetch) RUN_MODE=2 ;;
  default)    RUN_MODE=0 ;;
  n32_c0)     RUN_MODE=1 ;;
  *) echo "bad cfg $cfg"; exit 1 ;;
esac
[ -f $ko ] || { echo "no module $ko"; exit 1; }
RESULTS=$D/results.csv
[ -f $RESULTS ] || echo "workload,config,round,copies,copy,metric_name,metric,better,majflt,pswpin_all,prefetched_pages_all,prefetch_pages_used_all,elapsed_s,makespan_s,valid,command" > $RESULTS
check_hygiene
swapon --show | grep -q $(basename $SSD_SWAP) || { echo "SSD swap not on"; exit 1; }
swapon --show | grep -q ram0 && { echo "brd swap still on"; exit 1; }
drop_caches
sudo sysctl -q vm.page-cluster=3
[ -d $P ] && echo 0 | sudo tee $P/mode >/dev/null
sudo rmmod eden_swappf 2>/dev/null || true; sudo insmod $ko next_n=$n direct_map=0
# per-copy target: copy a -> target_tgid, copy b -> target_tgid2
set_target() {
    echo $1 > $W/target_pid
    echo $1 | sudo tee $P/$TPARAM >/dev/null
    echo $RUN_MODE | sudo tee $P/mode >/dev/null
}
CMD="corun.sh $WL $cfg $round $NC: $NC copies started together, cgroups swappf_${WL}_{a,b} memory.max=$WL_MEM each, shared SSD swap $SSD_SWAP, vm.page-cluster=3, insmod $(basename $ko) next_n=$n direct_map=0, mode=$RUN_MODE (0 = kernel read-ahead, 1 = trees, 2 = faulting page only), target_tgid/target_tgid2 = the copies; THP never; drop_caches before and after"
echo "[$(date +%T)] $CMD"
copies=(a b); pids=()
t0=$(date +%s.%N)
for i in $(seq 0 $((NC-1))); do
    c=${copies[$i]}
    W=$D/${cfg}_c${NC}_r${round}_$c; rm -rf $W; mkdir -p $W; echo "$CMD" > $W/settings
    CG=/sys/fs/cgroup/swappf_${WL}_$c; sudo rmdir $CG 2>/dev/null || true; sudo mkdir -p $CG; echo $WL_MEM | sudo tee $CG/memory.max >/dev/null
    TPARAM=$([ $c = a ] && echo target_tgid || echo target_tgid2)
    ( wl_run $W ) &
    pids+=($!)
done
rc=0; for p in "${pids[@]}"; do wait $p || rc=1; done
t1=$(date +%s.%N); mk=$(echo "$t1 - $t0" | bc)
echo 0 | sudo tee $P/mode >/dev/null
for i in $(seq 0 $((NC-1))); do
    c=${copies[$i]}; W=$D/${cfg}_c${NC}_r${round}_$c; CG=/sys/fs/cgroup/swappf_${WL}_$c
    [ -z "$(cat $CG/cgroup.procs)" ] || { echo "processes left in $CG"; exit 1; }
    cp $CG/memory.stat $W/memstat.txt
    a=($(cat $W/snap_begin)); b=($(cat $W/snap_end))
    el=$(echo "$(cat $W/t_end) - $(cat $W/t_begin)" | bc)
    echo "$WL,$cfg,$round,$NC,$c,$WL_METRIC,$(cat $W/metric),$WL_BETTER,$((b[0]-a[0])),$((b[1]-a[1])),$((b[2]-a[2])),$((b[3]-a[3])),$el,$mk,$(cat $W/valid),\"$CMD; $(cat $W/cmd | tr '\n' ' ')\"" | tee -a $RESULTS
done
drop_caches
