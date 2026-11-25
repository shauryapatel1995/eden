#!/bin/bash
# A bench workload under senpai (TMO-style proactive reclaim, /data1/shaurya/senpai_src/senpai/senpai_tmo.py,
# env RATIO (default 0.01), PSI threshold % (default 1.0), MAXR (default 0.01); senpai defaults are 0.0005 / 0.1 / 0.01): the workload runs in its own cgroup
# with NO memory limit; senpai reclaims it via memory.reclaim from process start. A sampler logs
# memory.current / anon / swap / PSI every second. Same metric + validity check as run.sh.
# Usage: [TREES=n4_c0] senpai_wl.sh <wl> <cfg: noprefetch|default|n<N>_c0> <round>
#   module = $WL_DIR/linux66b/eden_swappf_corun_$TREES.ko for every cfg (next_n N, direct_map 0); a tree cfg sets TREES;
#   mode 2 = faulting page only, 0 = kernel read-ahead (page-cluster 3), 1 = trees
. /data1/shaurya/linux66_bench/common.sh
WL=$1; cfg=$2; round=$3; . $B/wl/$WL.sh
L=$WL_DIR/linux66b; CG=/sys/fs/cgroup/senpai_$WL
SENPAI=/data1/shaurya/senpai_src/senpai/senpai_tmo.py; RATIO=${RATIO:-0.01}; PSI=${PSI:-1.0}; MAXR=${MAXR:-0.01}
TREES=${TREES:-n32_c0}   # tree set for the module: eden_swappf_corun_$TREES.ko
case $cfg in noprefetch) RUN_MODE=2 ;; default) RUN_MODE=0 ;; n*_c0) RUN_MODE=1; TREES=$cfg ;; *) echo "bad cfg"; exit 1 ;; esac
ko=$L/eden_swappf_corun_$TREES.ko; n=${TREES%_c*}; n=${n#n}
[ -f $ko ] || { echo "no module $ko"; exit 1; }
D=$L/runs_senpai_${WL}_r${RATIO}_p${PSI}; W=$D/${cfg}_r$round; RESULTS=$D/results.csv
mkdir -p $D; rm -rf $W; mkdir -p $W
[ -f $RESULTS ] || echo "workload,config,round,metric_name,metric,better,elapsed_s,mem_avg_mb,mem_peak_mb,mem_end_mb,mem_gbs,anon_avg_mb,swap_avg_mb,majflt,pswpin,prefetched_pages,prefetch_pages_used,psi_some_pct,senpai_ok,senpai_partial,valid,command" > $RESULTS
check_hygiene
swapon --show | grep -q $(basename $SSD_SWAP) || { echo "SSD swap not on"; exit 1; }
swapon --show | grep -q ram0 && { echo "brd swap still on"; exit 1; }
pgrep -f "[s]enpai_tmo.py" >/dev/null && { echo "a senpai is already running"; exit 1; }
drop_caches
sudo sysctl -q vm.page-cluster=3
sudo mkdir -p $CG; [ -z "$(cat $CG/cgroup.procs)" ] || { echo "processes left in $CG"; exit 1; }
for f in memory.max memory.high memory.swap.max; do echo max | sudo tee $CG/$f >/dev/null; done
[ -d $P ] && echo 0 | sudo tee $P/mode >/dev/null
sudo rmmod eden_swappf 2>/dev/null || true; sudo insmod $ko next_n=$n direct_map=0
CMD="senpai_wl.sh $WL $cfg $round: cgroup $CG memory.max/high/swap.max=max (no limit), SSD swap $SSD_SWAP, vm.page-cluster=3, insmod $(basename $ko) next_n=$n direct_map=0, mode=$RUN_MODE (0 = kernel read-ahead, 1 = trees, 2 = faulting page only); senpai: sudo python3 -I -u $SENPAI --reclaim-ratio $RATIO --psi-threshold $PSI --max-reclaim $MAXR $CG (defaults otherwise: interval 6, min_size 100M), started when the process is resumed; THP never; drop_caches before and after"
echo "$CMD" > $W/settings; echo "[$(date +%T)] $CMD"
psi_total() { awk '/^some/ {split($5, a, "="); print a[2]}' $CG/memory.pressure; }
cgs() { awk -v k=$1 '$1==k {print $2}' $CG/memory.stat; }
# start senpai + sampler right after the target is resumed
target_start() {
    local pid=$1
    echo $pid | sudo tee $CG/cgroup.procs >/dev/null
    set_target $pid
    psi_total > $W/psi_begin
    kill -CONT $pid
    sudo python3 -I -u $SENPAI --reclaim-ratio $RATIO --psi-threshold $PSI --max-reclaim $MAXR $CG > $W/senpai.log 2>&1 &
    echo "t,mem_current,anon,swap_current,swapcached,pgmajfault,psi_some_total" > $W/mem.csv
    ( while [ -d /proc/$pid ]; do
        echo "$(date +%s.%N),$(cat $CG/memory.current),$(cgs anon),$(cat $CG/memory.swap.current),$(cgs swapcached),$(cgs pgmajfault),$(psi_total)" >> $W/mem.csv
        sleep 1; done ) &
}
stop_senpai() { sudo pkill -f "[s]enpai_tmo.py.* $CG\$" || true; }
trap stop_senpai EXIT
( wl_run $W )
psi_total > $W/psi_end
stop_senpai; trap - EXIT; sleep 2
echo 0 | sudo tee $P/mode >/dev/null
pgrep -f "[s]enpai_tmo.py" >/dev/null && { echo "senpai still running"; exit 1; }
[ -z "$(cat $CG/cgroup.procs)" ] || { echo "processes left in $CG"; exit 1; }
cp $CG/memory.stat $W/memstat.txt
drop_caches
a=($(cat $W/snap_begin)); b=($(cat $W/snap_end))
el=$(echo "$(cat $W/t_end) - $(cat $W/t_begin)" | bc)
read mavg mpk mend mgbs aavg savg < <(python3 -I - $W/mem.csv <<'PY'
import csv, sys
rs = list(csv.DictReader(open(sys.argv[1])))
mb = lambda k: [int(r[k]) / 2**20 for r in rs]
t = [float(r['t']) for r in rs]; m = mb('mem_current')
gbs = sum((t[i+1] - t[i]) * m[i] for i in range(len(rs) - 1)) / 1024
print('%.1f %.1f %.1f %.1f %.1f %.1f' % (sum(m)/len(m), max(m), m[-1], gbs, sum(mb('anon'))/len(rs), sum(mb('swap_current'))/len(rs)))
PY
)
psi=$(echo "scale=4; 100 * ($(cat $W/psi_end) - $(cat $W/psi_begin)) / ($el * 1000000)" | bc)
echo "$WL,$cfg,$round,$WL_METRIC,$(cat $W/metric),$WL_BETTER,$el,$mavg,$mpk,$mend,$mgbs,$aavg,$savg,$((b[0]-a[0])),$((b[1]-a[1])),$((b[2]-a[2])),$((b[3]-a[3])),$psi,$(grep -c 'status=ok' $W/senpai.log),$(grep -c 'status=partial' $W/senpai.log),$(cat $W/valid),\"$CMD; $(cat $W/cmd | tr '\n' ' ')\"" | tee -a $RESULTS
