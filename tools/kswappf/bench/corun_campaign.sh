#!/bin/bash
# Co-run campaign: rounds 1..3 x configs x {1,2} copies, one run at a time.
# Usage: corun_campaign.sh [wl]   (default wiredtiger; module = $WL_DIR/linux66b/eden_swappf_corun_n32_c0.ko)
cd /data1/shaurya/linux66_bench
wl=${1:-wiredtiger}
for r in 1 2 3; do for cfg in noprefetch default n32_c0; do for nc in 1 2; do
  ./corun.sh $wl $cfg $r $nc || echo "FAILED $cfg r$r c$nc"
done; done; done
echo "[$(date +%T)] campaign done"
