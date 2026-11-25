#!/bin/bash
# Fixed-memory MPC sweep, one run at a time. Env: WLS, CFGS, PCTS, ROUNDS, TREES
cd /data1/shaurya/linux66_bench
for r in ${ROUNDS:-1}; do for pct in ${PCTS:-100 90 80 70 60 50 40 30}; do for wl in ${WLS:-gap_pr}; do for cfg in ${CFGS:-default n32_c0}; do
  ./mpc.sh $wl $cfg $pct $r || echo "FAILED $wl $cfg $pct r$r"
done; done; done; done
echo "[$(date +%T)] mpc campaign done"
