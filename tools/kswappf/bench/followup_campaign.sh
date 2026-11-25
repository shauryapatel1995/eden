#!/bin/bash
# Order (user, 2026-11-25): redis_stack first, then the rest of the Leap campaign, then the WT re-runs.
# All one run at a time. Results: <WL_DIR>/linux66b/runs_leap/results.csv. Resumable.
# 1) redis_stack (DiLOS Redis 6.2.4 LRANGE, 20M LPUSH, memory.max 2560M as the stack-tree runs), run2.sh:
#    noprefetch, default, leapc, st_ext4 (spatial + stack trees), 5 rounds, configs interleaved per round.
# 2) leap_campaign.sh (CFG=leapc) for the remaining workloads.
# 3) WiredTiger YCSB-E + YCSB-C same-kernel re-runs (run2.sh): noprefetch, default, n32_c0, 5 rounds.
B=/data1/shaurya/linux66_bench
while ps -eo args | grep -q "^/bin/bash $B/run.sh"; do sleep 20; done    # the run in flight
run() {   # wl cfg round
  local L=$(bash -c ". $B/wl/$1.sh; echo \$WL_DIR")/linux66b
  grep -q "^$1,$2,$3," $L/runs_leap/results.csv 2>/dev/null && return
  echo "[$(date +%T)] start $1 $2 r$3"
  RUNS=runs_leap $B/run2.sh $1 $2 $3 2>&1 | tail -2
}
for r in 1 2 3 4 5; do for c in noprefetch default leapc st_ext4; do run redis_stack $c $r; done; done
echo "[$(date +%T)] redis_stack done"
CFG=leapc $B/leap_campaign.sh
for r in 1 2 3 4 5; do for w in wiredtiger wt_c; do for c in noprefetch default n32_c0; do run $w $c $r; done; done; done
echo "[$(date +%T)] follow-ups done"
