#!/bin/bash
# Redis under senpai (TMO-style proactive reclaim, /data1/shaurya/senpai_src/senpai/senpai_tmo.py,
# default settings except --reclaim-ratio RATIO, default 0.005 = 10x senpai's): the server runs in its own cgroup with no memory limit; senpai reclaims it via
# memory.reclaim from server start. Load = 2M random_lpush (1KB), then random_lrange_600 for DURATION
# seconds (default 1200) in 100k-request chunks. A sampler logs memory.current etc. every second.
# Usage: senpai_redis.sh <default|n<N>_c<0|1>> <round>
#   default     Linux read-ahead (module off, page-cluster 3)
#   n<N>_c<0|1> eden_swappf mode 1 with those trees (direct_map 0)
set -eu
B=/data1/shaurya/linux66_bench
P=/sys/module/eden_swappf/parameters
L=/data1/shaurya/deku_redis/linux66b
R=/data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src
SENPAI=/data1/shaurya/senpai_src/senpai/senpai_tmo.py
CG=/sys/fs/cgroup/senpai_redis
SSD_SWAP=/data1/swapfile_swappf
DURATION=${DURATION:-1200}
RATIO=${RATIO:-0.005}   # senpai --reclaim-ratio (its default is 0.0005)
cfg=$1; round=$2
case $cfg in
  default) RUN_MODE=0; ko=$L/eden_swappf_n4_c0.ko; n=4 ;;
  n*_c*)   RUN_MODE=1; ko=$L/eden_swappf_$cfg.ko; n=${cfg%_c*}; n=${n#n} ;;
  *) echo "bad cfg $cfg"; exit 1 ;;
esac
D=$L/runs_senpai_ratio$RATIO; W=$D/${cfg}_r$round; RESULTS=$D/results.csv
mkdir -p $D; rm -rf $W; mkdir -p $W; cd $W
[ -f $RESULTS ] || echo "config,round,duration_s,lrange_requests,lrange_rps_mean,mem_avg_mb,mem_min_mb,mem_end_mb,anon_avg_mb,swap_avg_mb,memcur_avg_load_mb,majflt,pswpin,swap_ra,swap_ra_hit,mod_predicted,psi_some_pct,senpai_reclaim_ok,senpai_reclaim_partial" > $RESULTS

# hygiene (swap-run-hygiene): THP never, SSD swap only, caches dropped before and after
for f in enabled defrag; do grep -q '\[never\]' /sys/kernel/mm/transparent_hugepage/$f || { echo "THP $f not never"; exit 1; }; done
swapon --show | grep -q $(basename $SSD_SWAP) || { echo "SSD swap not on"; exit 1; }
swapon --show | grep -q ram0 && { echo "brd swap still on"; exit 1; }
$R/redis-cli -p 6390 ping >/dev/null 2>&1 && { echo "port 6390 busy"; exit 1; }
pgrep -f "[s]enpai_tmo.py" >/dev/null && { echo "a senpai is already running"; exit 1; }
drop() { sync; echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null; }
drop
sudo sysctl -q vm.page-cluster=3
sudo mkdir -p $CG
[ -z "$(cat $CG/cgroup.procs)" ] || { echo "processes left in $CG"; exit 1; }
for f in memory.max memory.high memory.swap.max; do echo max | sudo tee $CG/$f >/dev/null; done
[ -d $P ] && echo 0 | sudo tee $P/mode >/dev/null
sudo rmmod eden_swappf 2>/dev/null || true; sudo insmod $ko next_n=$n direct_map=0

vm() { awk -v k=$1 '$1==k {print $2}' /proc/vmstat; }
cgs() { awk -v k=$1 '$1==k {print $2}' $CG/memory.stat; }
psi_total() { awk '/^some/ {split($5, a, "="); print a[2]}' $CG/memory.pressure; }
snap() { echo "$(cgs pgmajfault) $(vm pswpin) $(vm swap_ra) $(vm swap_ra_hit) $(cat $P/predicted_pages) $(psi_total)"; }

echo "$B/senpai_redis.sh $cfg $round: cgroup $CG memory.max/high/swap.max=max; vm.page-cluster=3; insmod $ko next_n=$n direct_map=0; eden_swappf mode=$RUN_MODE (0 = off/kernel read-ahead, 1 = trees); THP never; drop_caches before and after; senpai: sudo python3 -I -u $SENPAI --reclaim-ratio $RATIO $CG (reclaim_ratio $RATIO; defaults otherwise: interval 6, psi_threshold 0.1, max_reclaim 0.01, min_size 100M) started with the server; LRANGE phase $DURATION s" > settings
{ echo "server (in cgroup): cd $R; ./redis-server --port 6390 --save '' --appendonly no"
  echo "senpai: sudo python3 -I -u $SENPAI --reclaim-ratio $RATIO $CG"
  echo "load (native): ./redis-benchmark -p 6390 -t random_lpush -d 1024 -n 2000000 -r 100000"
  echo "measured (native), repeated for $DURATION s: ./redis-benchmark -p 6390 -t random_lrange_600 -d 1024 -n 100000 -r 100000"; } > cmd

sh -c 'kill -STOP $$; cd '$R'; exec ./redis-server --port 6390 --save "" --appendonly no' > server.log 2>&1 &
spid=$!; sleep 0.3
echo $spid | sudo tee $CG/cgroup.procs >/dev/null
echo $spid | sudo tee $P/target_tgid >/dev/null; echo $RUN_MODE | sudo tee $P/mode >/dev/null
kill -CONT $spid
sudo python3 -I -u $SENPAI --reclaim-ratio $RATIO $CG > senpai.log 2>&1 &
senpai_pid=$!
# sampler: t, phase, memory.current, anon, swap.current, swapcached, pgmajfault, psi some total
echo "t,phase,mem_current,anon,swap_current,swapcached,pgmajfault,psi_some_total" > mem.csv
( while [ -d /proc/$spid ]; do
    echo "$(date +%s.%N),$(cat phase 2>/dev/null || echo start),$(cat $CG/memory.current),$(cgs anon),$(cat $CG/memory.swap.current),$(cgs swapcached),$(cgs pgmajfault),$(psi_total)" >> mem.csv
    sleep 1; done ) &
sampler=$!
cleanup() { sudo kill $senpai_pid 2>/dev/null || true; sudo pkill -f "[s]enpai_tmo.py.* $CG\$" || true; kill $sampler 2>/dev/null || true
            $R/redis-cli -p 6390 shutdown nosave >/dev/null 2>&1 || true; wait $spid 2>/dev/null || true
            echo 0 | sudo tee $P/mode >/dev/null; }
trap cleanup EXIT

for _ in $(seq 1 120); do $R/redis-cli -p 6390 ping 2>/dev/null | grep -q PONG && break; sleep 0.5; done
echo load > phase
(cd $R && ./redis-benchmark -p 6390 -t random_lpush -d 1024 -n 2000000 -r 100000) > lpush.log 2>&1
echo lrange > phase
snap > snap_begin; t0=$(date +%s.%N); echo $t0 > t_begin
chunks=0
while [ $(echo "$(date +%s.%N) - $t0 < $DURATION" | bc) = 1 ]; do
    (cd $R && ./redis-benchmark -p 6390 -t random_lrange_600 -d 1024 -n 100000 -r 100000) > lrange_last.log 2>&1
    tr '\r' '\n' < lrange_last.log | grep -q "100000 requests completed" || { echo "LRANGE chunk failed"; cat lrange_last.log; exit 1; }
    echo "$(date +%s.%N),$(tr '\r' '\n' < lrange_last.log | grep 'requests per second' | tail -1 | awk '{print $1}')" >> chunks.csv
    chunks=$((chunks + 1))
done
snap > snap_end; t1=$(date +%s.%N); echo $t1 > t_end; echo done > phase
cleanup; trap - EXIT
sleep 1; pgrep -f "[s]enpai_tmo.py" >/dev/null && { echo "senpai still running"; exit 1; }
[ -z "$(cat $CG/cgroup.procs)" ] || { echo "processes left in $CG: $(cat $CG/cgroup.procs)"; exit 1; }
cp $CG/memory.stat memstat.txt
drop

a=($(cat snap_begin)); b=($(cat snap_end))
dur=$(echo "$t1 - $t0" | bc)
read mem_avg mem_min mem_end anon_avg swap_avg load_avg < <(python3 -I - mem.csv <<'EOF'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
lr = [r for r in rows if r['phase'] == 'lrange'] or rows
ld = [r for r in rows if r['phase'] == 'load'] or rows
mb = lambda rs, k: [int(r[k]) / 2**20 for r in rs]
avg = lambda xs: sum(xs) / len(xs)
print('%.1f %.1f %.1f %.1f %.1f %.1f' % (avg(mb(lr, 'mem_current')), min(mb(lr, 'mem_current')), mb(lr, 'mem_current')[-1],
      avg(mb(lr, 'anon')), avg(mb(lr, 'swap_current')), avg(mb(ld, 'mem_current'))))
EOF
)
rps=$(awk -F, '{s += $2; n++} END {printf "%.1f", s / n}' chunks.csv)
psi=$(echo "scale=4; 100 * (${b[5]} - ${a[5]}) / ($dur * 1000000)" | bc)
echo "$cfg,$round,$dur,$((chunks * 100000)),$rps,$mem_avg,$mem_min,$mem_end,$anon_avg,$swap_avg,$load_avg,$((b[0]-a[0])),$((b[1]-a[1])),$((b[2]-a[2])),$((b[3]-a[3])),$((b[4]-a[4])),$psi,$(grep -c 'status=ok' senpai.log),$(grep -c 'status=partial' senpai.log)" | tee -a $RESULTS
