#!/bin/bash
# senpai pilot: gap_pr_long, read-ahead vs n32_c0 trees, one run at a time
cd /data1/shaurya/linux66_bench
for r in ${ROUNDS:-1}; do for cfg in ${CFGS:-default n32_c0}; do
  ./senpai_wl.sh ${WLS:-gap_pr_long} $cfg $r || echo "FAILED $cfg r$r"
done; done
echo "[$(date +%T)] senpai campaign done"
