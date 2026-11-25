#!/bin/bash
# One timed run of one workload over SSD swap. Caches are dropped before and after,
# the module is reloaded (zeroed counters) and the app is killed before the next run.
# run2.sh: run.sh for the current kernel (#7): the main-campaign modules no longer load, so noprefetch /
# default / leap / leapc use $B/eden_swappf_leap.ko (current source, empty trees) and a tree config <cfg>
# uses $L/eden_swappf_corun_<cfg>.ko (current source, the workload's trees) with KO_ARGS_<cfg> from the
# workload file if set, else next_n from the n<N>_ name.
# Usage: run2.sh <wl> <cfg> <round>
#   cfg: noprefetch  module mode 2 (faulting page only)
#        default     Linux default read-ahead (module off, page-cluster 3)
#        n<N>_c<0|1> module mode 1 with trees trained on +1..+N, claim rule on (1) / off (0)
#        n<N>_c<0|1>_m<0|1|2>  same with direct_map 0 (swap cache only) / 1 (sync map) / 2 (async map)
#        leap        module mode 3: Leap's majority-trend prefetcher (Hsize 32, PWsize_max 8), no trees
#                    (eden_swappf_leap.ko in $B, built from eden_swappf.c with empty trees); window = paper's Alg. 2
#        leapc       same, window/fallback of Leap's released code (window >= 4, aligned-block fallback)
#   RUNS=<dir> (default runs): results go to $L/$RUNS
. /data1/shaurya/linux66_bench/common.sh
load_wl $1; cfg=$2; round=$3
D=$L/${RUNS:-runs}; mkdir -p $D; W=$D/${cfg}_r$round
ko=$B/eden_swappf_leap.ko; n=4; DMAP=0; KOX=""
case $cfg in
  noprefetch) RUN_MODE=2 ;;
  default)    RUN_MODE=0 ;;
  leap)       RUN_MODE=3; KOX="leap_variant=0" ;;
  leapc)      RUN_MODE=3; KOX="leap_variant=1" ;;
  *)          RUN_MODE=1; ko=$L/eden_swappf_corun_$cfg.ko
              v=KO_ARGS_$cfg; KOX=${!v:-}
              case $cfg in n*_c*) n=${cfg%%_*}; n=${n#n} ;; esac ;;
esac
[ -f $ko ] || { echo "no module $ko"; exit 1; }
RESULTS=$D/results.csv
[ -f $RESULTS ] || echo "workload,config,round,metric_name,metric,better,majflt,pswpin,prefetched_pages,prefetch_pages_used,mod_faults,mod_predicted,mapped_pages,map_skipped,elapsed_s,valid" > $RESULTS
head -1 $RESULTS | grep -q mapped_pages || { echo "$RESULTS has the pre-#6 columns; use RUNS=<new dir>"; exit 1; }
check_hygiene
swapon --show | grep -q $(basename $SSD_SWAP) || { echo "SSD swap not on"; exit 1; }
swapon --show | grep -q ram0 && { echo "brd swap still on"; exit 1; }
drop_caches
sudo sysctl -q vm.page-cluster=3
[ -d $P ] && echo 0 | sudo tee $P/mode >/dev/null
sudo rmmod eden_swappf 2>/dev/null || true; sudo insmod $ko next_n=$n direct_map=$DMAP $KOX
rm -rf $W; mkdir -p $W
type wl_prepare >/dev/null 2>&1 && wl_prepare $W
echo "$B/run2.sh $WL $cfg $round: cgroup $CG memory.max=$(cat $CG/memory.max); vm.page-cluster=$(cat /proc/sys/vm/page-cluster); insmod $ko next_n=$n direct_map=$DMAP $KOX; eden_swappf mode=$RUN_MODE (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=$DMAP (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after" > $W/settings
echo "[$(date +%T)] $WL $cfg r$round: mode=$RUN_MODE next_n=$(cat $P/next_n) direct_map=$(cat $P/direct_map) page-cluster=$(cat /proc/sys/vm/page-cluster)"
wl_run $W
echo 0 | sudo tee $P/mode >/dev/null
[ -z "$(cat $CG/cgroup.procs)" ] || { echo "processes left in $CG: $(cat $CG/cgroup.procs)"; exit 1; }
cp $CG/memory.stat $W/memstat.txt
drop_caches
a=($(cat $W/snap_begin)); b=($(cat $W/snap_end))
el=$(echo "$(cat $W/t_end) - $(cat $W/t_begin)" | bc)
echo "$WL,$cfg,$round,$WL_METRIC,$(cat $W/metric),$WL_BETTER,$((b[0]-a[0])),$((b[1]-a[1])),$((b[2]-a[2])),$((b[3]-a[3])),$((b[4]-a[4])),$((b[5]-a[5])),$((b[6]-a[6])),$((b[7]-a[7])),$el,$(cat $W/valid)" | tee -a $RESULTS
