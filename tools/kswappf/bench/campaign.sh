#!/bin/bash
# All timed runs, one at a time: for each workload, ROUNDS rounds of the six configs.
# Resumable: a (workload, config, round) already in results.csv is skipped. A failed run
# is logged, its cgroup is killed and the campaign moves on. Usage: campaign.sh <wl>...
B=/data1/shaurya/linux66_bench; ROUNDS=${ROUNDS:-5}
CFGS=${CFGS:-"noprefetch default n4_c1 n4_c0 n32_c1 n32_c0"}
for wl in "$@"; do
  d=$(grep -oP '^WL_DIR=\K[^;]+' $B/wl/$wl.sh); R=$d/linux66b/runs/results.csv
  for r in $(seq 1 $ROUNDS); do
    for cfg in $CFGS; do
      [ -f $R ] && grep -q "^$wl,$cfg,$r," $R && continue
      $B/run.sh $wl $cfg $r >> $B/logs/campaign_$wl.log 2>&1 || {
        echo "[$(date +%T)] FAILED $wl $cfg r$r (see logs/campaign_$wl.log)" | tee -a $B/logs/campaign_failures.log
        echo 1 | sudo tee /sys/fs/cgroup/swappf_$wl/cgroup.kill >/dev/null 2>&1; sleep 2
        echo 0 | sudo tee /sys/module/eden_swappf/parameters/mode >/dev/null 2>&1
        sync; echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null; }
      tail -1 $R 2>/dev/null | grep "^$wl,$cfg,$r,"
    done
  done
  python3 $B/aggregate.py
done
