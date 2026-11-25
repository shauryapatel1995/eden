#!/bin/bash
# One point of a fixed-memory MPC sweep: <wl> at memory.max = <pct>% of its native anon peak,
# PSI "some" (cgroup memory.pressure) measured over the whole process, sampled every second.
# Module = $WL_DIR/linux66b/eden_swappf_corun_$TREES.ko (current source; else the campaign eden_swappf_$TREES.ko) for every cfg (TREES default n32_c0).
# TIMEOUT=<s>: kill the cgroup (cgroup.kill) after that many seconds; the row is then marked valid=killed.
# Usage: [TREES=n4_c0] mpc.sh <wl> <cfg: noprefetch|default|n<N>_c0> <pct> <round>
. /data1/shaurya/linux66_bench/common.sh
WL=$1; cfg=$2; pct=$3; round=$4
. $B/wl/$WL.sh; L=$WL_DIR/linux66b
PEAK=$(cat $L/native/anon_peak)                     # bytes
MEMB=$(( PEAK * pct / 100 / 4096 * 4096 ))
WL_MEM_OVERRIDE=$MEMB; load_wl $WL                    # sets CG, memory.max
TREES=${TREES:-n32_c0}
case $cfg in noprefetch) RUN_MODE=2 ;; default) RUN_MODE=0 ;; n*_c0|st_*) RUN_MODE=1; TREES=$cfg ;; *) echo "bad cfg"; exit 1 ;; esac
ko=$L/eden_swappf_corun_$TREES.ko; [ -f $ko ] || ko=$L/eden_swappf_$TREES.ko   # current-source module first, else the campaign one
n=${TREES%_c*}; n=${n#n}
[ -f $ko ] || { echo "no module $ko"; exit 1; }
D=$L/runs_mpc; W=$D/${cfg}_p${pct}_r$round; RESULTS=$D/results.csv
mkdir -p $D; rm -rf $W; mkdir -p $W
[ -f $RESULTS ] || echo "workload,config,pct,round,mem_limit_mb,anon_peak_mb,metric_name,metric,better,elapsed_s,psi_some_pct,psi_full_pct,psi_some_p95_10s,mem_avg_mb,savings_avg_mb,savings_pct,majflt,pswpin,prefetched_pages,prefetch_pages_used,valid,command" > $RESULTS
check_hygiene
swapon --show | grep -q $(basename $SSD_SWAP) || { echo "SSD swap not on"; exit 1; }
swapon --show | grep -q ram0 && { echo "brd swap still on"; exit 1; }
drop_caches
sudo sysctl -q vm.page-cluster=3
sudo rmdir $CG 2>/dev/null; sudo mkdir -p $CG; echo $MEMB | sudo tee $CG/memory.max >/dev/null
[ -d $P ] && echo 0 | sudo tee $P/mode >/dev/null
DM=""; modinfo -p $ko | grep -q "^direct_map:" && DM="direct_map=0"   # older modules have no direct_map
KA=$(eval echo \${KO_ARGS_$TREES:-})   # workload-specific module args (e.g. stack trees), else next_n
[ -n "$KA" ] && { n=$KA; DM=""; } || n="next_n=$n"
sudo rmmod eden_swappf 2>/dev/null || true; sudo insmod $ko $n $DM || { echo "insmod $ko failed"; exit 1; }
CMD="mpc.sh $WL $cfg $pct $round${TIMEOUT:+ (TIMEOUT=$TIMEOUT s)}: cgroup $CG memory.max=$MEMB ($pct% of native anon peak $PEAK B), SSD swap $SSD_SWAP, vm.page-cluster=3, insmod $(basename $ko) $n $DM, mode=$RUN_MODE (0 = kernel read-ahead, 1 = trees, 2 = faulting page only); PSI from $CG/memory.pressure over the whole process; THP never; drop_caches before and after"
type wl_prepare >/dev/null 2>&1 && wl_prepare $W   # input files (SPEC, lbm, xalan), as run.sh
echo "$CMD" > $W/settings; echo "[$(date +%T)] $CMD"
psi() { awk -v k=$1 '$1==k {split($5, a, "="); print a[2]}' $CG/memory.pressure; }
target_start() {
    local pid=$1
    echo $pid | sudo tee $CG/cgroup.procs >/dev/null
    set_target $pid
    echo "$(psi some) $(psi full)" > $W/psi_begin
    kill -CONT $pid
    echo "t,mem_current,psi_some_total" > $W/mem.csv
    ( while [ -d /proc/$pid ]; do echo "$(date +%s.%N),$(cat $CG/memory.current),$(psi some)" >> $W/mem.csv; sleep 1; done ) &
}
( if [ -n "${TIMEOUT:-}" ]; then ( exec >/dev/null 2>&1; sleep $TIMEOUT; [ -f $W/t_end ] || { echo killed > $W/killed; echo 1 | sudo tee $CG/cgroup.kill >/dev/null; } ) & wd=$!; fi
  wl_run $W
  if [ -n "${wd:-}" ]; then kill $wd 2>/dev/null || true; fi; true ) || true
[ -f $W/killed ] && { echo killed > $W/valid; [ -f $W/t_end ] || date +%s.%N > $W/t_end; [ -f $W/snap_end ] || cp $W/snap_begin $W/snap_end 2>/dev/null; [ -s $W/metric ] || echo NA > $W/metric; }
echo "$(psi some) $(psi full)" > $W/psi_end
echo 0 | sudo tee $P/mode >/dev/null
[ -z "$(cat $CG/cgroup.procs)" ] || { echo "processes left in $CG"; exit 1; }
cp $CG/memory.stat $W/memstat.txt
drop_caches
a=($(cat $W/snap_begin)); b=($(cat $W/snap_end)); p0=($(cat $W/psi_begin)); p1=($(cat $W/psi_end))
el=$(echo "$(cat $W/t_end) - $(cat $W/t_begin)" | bc)
read mavg p95 < <(python3 -I - $W/mem.csv <<'PY'
import csv, sys
rs = list(csv.DictReader(open(sys.argv[1])))
m = [int(r['mem_current']) / 2**20 for r in rs]
t = [float(r['t']) for r in rs]; s = [int(r['psi_some_total']) for r in rs]
w = [100 * (s[i+10] - s[i]) / ((t[i+10] - t[i]) * 1e6) for i in range(0, len(rs) - 10, 10)] or [0]
w.sort()
print('%.1f %.3f' % (sum(m) / len(m), w[min(len(w) - 1, int(0.95 * len(w)))]))
PY
)
pk=$(echo "$PEAK / 1048576" | bc); lim=$(echo "$MEMB / 1048576" | bc)
sv=$(echo "$pk - $mavg" | bc); svp=$(echo "scale=2; 100 * $sv / $pk" | bc)
ps=$(echo "scale=4; 100 * (${p1[0]} - ${p0[0]}) / ($el * 1000000)" | bc); pf=$(echo "scale=4; 100 * (${p1[1]} - ${p0[1]}) / ($el * 1000000)" | bc)
echo "$WL,$cfg,$pct,$round,$lim,$pk,$WL_METRIC,$(cat $W/metric),$WL_BETTER,$el,$ps,$pf,$p95,$mavg,$sv,$svp,$((b[0]-a[0])),$((b[1]-a[1])),$((b[2]-a[2])),$((b[3]-a[3])),$(cat $W/valid),\"$CMD; $(cat $W/cmd | tr '\n' ' ')\"" | tee -a $RESULTS
