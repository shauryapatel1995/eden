#!/bin/bash
# brd (RAM) swap-in trace of one workload: native reference run first (unlimited memory),
# then the same run in the cgroup over brd swap with the swapin:swapin_fault tracepoint
# filtered to the target. Usage: trace.sh <wl>
. /data1/shaurya/linux66_bench/common.sh
load_wl $1
check_hygiene
pgrep -f "cat $T/trace_pipe" >/dev/null && { echo "trace_pipe reader running"; exit 1; }
sudo rmmod eden_swappf 2>/dev/null || true
W=$L/native; rm -rf $W; mkdir -p $W; drop_caches
echo max | sudo tee $CG/memory.max >/dev/null
type wl_prepare >/dev/null 2>&1 && wl_prepare $W
MODE_NATIVE=1 MODE_TRACE=0 RUN_MODE=0
sudo rmdir $CG 2>/dev/null || true; sudo mkdir -p $CG    # fresh memory.peak
wl_run $W
echo "native: metric=$(cat $W/metric) valid=$(cat $W/valid) peak_bytes=$(cat $CG/memory.peak)"
MODE_NATIVE=0
echo ${WL_MEM_TRACE:-$WL_MEM} | sudo tee $CG/memory.max >/dev/null
sudo swapoff $SSD_SWAP
sudo modprobe brd rd_nr=1 rd_size=$((64*1024*1024))
sudo mkswap /dev/ram0 >/dev/null; sudo swapon -p 100 /dev/ram0
sudo sysctl -q vm.page-cluster=0
W=$L/trace; rm -rf $W; mkdir -p $W; drop_caches
type wl_prepare >/dev/null 2>&1 && wl_prepare $W
echo "trace memory.max $(cat $CG/memory.max)"
echo 262144 | sudo tee $T/buffer_size_kb >/dev/null
echo | sudo tee $T/trace >/dev/null
sudo sh -c "cat $T/trace_pipe > $W/swapin.trace" &
MODE_TRACE=1
wl_run $W || true
echo 0 | sudo tee $T/events/swapin/swapin_fault/enable >/dev/null
# let the reader drain the ring buffer first: a fast-faulting target can leave GBs unread, and
# killing the reader then silently drops the end of the run (not counted as overruns)
for _ in $(seq 1 7200); do
    e=$(sudo sh -c "cat $T/per_cpu/cpu*/stats" | awk '/^entries:/ {s+=$2} END {print s+0}')
    [ "$e" = 0 ] && break; sleep 1
done
echo "trace buffer drained (unread entries: $e)"
sleep 2; sudo pkill -f "cat $T/trace_pipe" || true
lost=$(sudo sh -c "cat $T/per_cpu/cpu*/stats" | awk '/overrun|dropped/ {s+=$NF} END {print s+0}')
echo "trace: metric=$(cat $W/metric) valid=$(cat $W/valid) faults=$(grep -c swapin_fault $W/swapin.trace) lost_events=$lost"
sudo swapoff /dev/ram0; sudo rmmod brd; sudo swapon -p 100 $SSD_SWAP
sudo sysctl -q vm.page-cluster=3
drop_caches
python3 /home/shaurya/eden/tools/fltrace/swapin_trace_to_eden.py $W/swapin.trace $L/raw.out --tgid $(cat $W/target_pid)
