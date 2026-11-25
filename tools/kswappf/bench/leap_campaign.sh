#!/bin/bash
# Leap (module mode 3; CFG=leapc (default, released-code window) or leap (paper window)) over every reported workload, 5 rounds each, fastest workload first
# (order = default-config run time in the main campaign). Same memory.max (WL_MEM) and run.sh as
# every other config. Results: <WL_DIR>/linux66b/runs_leap/results.csv. Resumable: a round with a
# row in that file is skipped. Usage: leap_campaign.sh [rounds (default 5)]
B=/data1/shaurya/linux66_bench
WLS="redis cactu gap_sssp gap_pr mcf memcached wt_c wiredtiger xalan_ref xalan dataframe cactu_ref etc memtier mcf_ref webservice lbm_ref"
ROUNDS=${1:-5}; CFG=${CFG:-leapc}
for w in $WLS; do
  L=$(bash -c ". $B/wl/$w.sh; echo \$WL_DIR")/linux66b
  for r in $(seq 1 $ROUNDS); do
    grep -q "^$w,$CFG,$r," $L/runs_leap/results.csv 2>/dev/null && continue
    echo "[$(date +%T)] start $w $CFG r$r"
    RUNS=runs_leap $B/run.sh $w $CFG $r 2>&1 | tail -2
  done
done
echo "[$(date +%T)] leap campaign done"
