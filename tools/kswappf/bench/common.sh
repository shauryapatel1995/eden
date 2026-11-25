# Shared by trace.sh / train.sh / run.sh. A workload file wl/<name>.sh defines:
#   WL_DIR    data dir (traces, trees, modules, runs live in $WL_DIR/linux66b)
#   WL_MEM    cgroup memory.max (same as the eden local memory)
#   WL_METRIC metric name; WL_BETTER = lower|higher
#   wl_prepare <workdir>       copy inputs into the run dir (optional)
#   wl_run <workdir>           run the app. It must start the target process stopped,
#                              call `target_start <pid>` (moves it into the cgroup, arms
#                              tracing / the prefetcher, resumes it), call measure_begin /
#                              measure_end around the measured phase, write the metric to
#                              <workdir>/metric and "pass"/"FAIL" to <workdir>/valid, and
#                              leave no process behind.
set -eu
B=/data1/shaurya/linux66_bench
P=/sys/module/eden_swappf/parameters
T=/sys/kernel/tracing
SSD_SWAP=/data1/swapfile_swappf
vm() { awk -v k=$1 '$1==k {print $2}' /proc/vmstat; }
cgstat() { awk -v k=$1 '$1==k {print $2}' $CG/memory.stat; }
load_wl() {
    WL=$1; . $B/wl/$WL.sh
    L=$WL_DIR/linux66b; mkdir -p $L
    CG=/sys/fs/cgroup/swappf_$WL
    sudo mkdir -p $CG; echo $WL_MEM | sudo tee $CG/memory.max >/dev/null
}
# Settings every run needs (swap-run hygiene): THP never, caches dropped.
check_hygiene() {
    for f in enabled defrag; do grep -q '\[never\]' /sys/kernel/mm/transparent_hugepage/$f || { echo "THP $f not never"; exit 1; }; done
}
drop_caches() { sync; echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null; }
# Called by wl_run with the stopped target's pid: cgroup, tracing / prefetcher, resume.
target_start() {
    local pid=$1
    echo $pid | sudo tee $CG/cgroup.procs >/dev/null
    set_target $pid
    kill -CONT $pid
}
# Point the tracepoint filter / prefetcher at <pid> (wl_run may call it again when the
# process doing the work is a child, e.g. Neo4j's server JVM).
set_target() {
    local pid=$1
    echo $pid > $W/target_pid
    if [ "${MODE_TRACE:-0}" = 1 ]; then
        echo "tgid == $pid" | sudo tee $T/events/swapin/swapin_fault/filter >/dev/null
        echo 1 | sudo tee $T/events/swapin/swapin_fault/enable >/dev/null
    elif [ "${MODE_NATIVE:-0}" != 1 ]; then
        echo $pid | sudo tee $P/target_tgid >/dev/null
        echo $RUN_MODE | sudo tee $P/mode >/dev/null
    fi
}
# Single-process app: run "<cmd>" in the cwd from start to exit, measured end to end.
# stdout/stderr go to stdout.out / stderr.log; metric = wall seconds.
run_single() {
    echo "(cwd $PWD) $1 > stdout.out 2> stderr.log" >> cmd
    sh -c 'kill -STOP $$; exec '"$1" > stdout.out 2> stderr.log &
    local pid=$!; sleep 0.3
    measure_begin; target_start $pid; local rc=0; wait $pid || rc=$?; measure_end
    echo $rc > exitcode
    echo "$(cat t_end) - $(cat t_begin)" | bc > metric
}
spec_env() { cd /data1/shaurya/spec2017 && . ./shrc >/dev/null 2>&1; cd - >/dev/null; }
# specdiff <args...>: pass if it reports only "specdiff run completed"
specdiff_ok() { local o; o=$(specdiff "$@" 2>&1) || true; [ "$(echo "$o" | grep -c .)" = 1 ] && echo "$o" | grep -q "specdiff run completed"; }
# Counters over the measured phase: cgroup major faults, system swap-in / read-ahead
# (swap_ra = pages read ahead by the kernel or the module, swap_ra_hit = those used).
snap() { echo "$(cgstat pgmajfault) $(vm pswpin) $(vm swap_ra) $(vm swap_ra_hit) $(cat $P/faults 2>/dev/null || echo 0) $(cat $P/predicted_pages 2>/dev/null || echo 0) $(cat $P/mapped_pages 2>/dev/null || echo 0) $(cat $P/map_skipped 2>/dev/null || echo 0)"; }
measure_begin() { snap > $W/snap_begin; date +%s.%N > $W/t_begin; }
measure_end()   { snap > $W/snap_end;   date +%s.%N > $W/t_end; }
