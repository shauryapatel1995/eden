# Redis + memtier_benchmark, following Hermit's Memcached workload (Qiao et al., NSDI '23, Table 1 and Sec. 6.1):
# Facebook USR-like key-value pairs (2-byte values, ~16-byte keys), keys Zipf, 99.8% GET / 0.2% SET.
# Training input (differs from the test: 24M keys, Zipf 0.9, 5x the requests); trees for memtier: 24000000 keys, Zipf exponent 0.9, 100000 requests per client (4 threads x 50 clients).
# Load: --ratio=1:0 --key-pattern=P:P -n allkeys. Server in the cgroup (redis 5.0.7, as wl/redis.sh), memtier native.
# Metric = memtier Totals ops/sec (p99 latency is in meas.log). Valid = memtier exited 0, Totals line, no errors.
WL_DIR=/data1/shaurya/deku_memtier_train; WL_MEM=${WL_MEM_OVERRIDE:-1228M}; WL_METRIC=ops_per_sec; WL_BETTER=higher   # 50% of native anon peak (2.40GB)
MT=/data1/shaurya/deku_memtier/install/bin/memtier_benchmark
wl_run() ( cd $1; R=/data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src
    cli() { $R/redis-cli -p 6390 "$@"; }
    cli ping >/dev/null 2>&1 && { echo "port 6390 busy"; exit 1; }
    sh -c 'kill -STOP $$; cd '$R'; exec ./redis-server --port 6390 --save "" --appendonly no' > server.log 2>&1 &
    spid=$!; sleep 0.3; target_start $spid
    common="-s 127.0.0.1 -p 6390 -t 4 -c 50 -d 2 --key-minimum=1 --key-maximum=24000000 --hide-histogram"
    load="$MT $common --ratio=1:0 --key-pattern=P:P -n allkeys"
    meas="$MT $common --ratio=1:499 --key-pattern=Z:Z --key-zipf-exp=0.9 -n 100000"
    { echo "server (in cgroup): cd $R; ./redis-server --port 6390 --save '' --appendonly no"
      echo "load (native): $load"; echo "measured (native): $meas"; } >> cmd
    for _ in $(seq 1 120); do cli ping 2>/dev/null | grep -q PONG && break; sleep 0.5; done
    $load > load.log 2>&1
    measure_begin
    # trace mode: mark where the measured phase starts (training uses its faults only)
    [ "${MODE_TRACE:-0}" = 1 ] && echo MEASURE_BEGIN | sudo tee /sys/kernel/tracing/trace_marker >/dev/null
    rc=0; $meas > meas.log 2>&1 || rc=$?
    measure_end
    cli shutdown nosave >/dev/null 2>&1 || true; wait $spid || true
    awk '/^Totals/ {print $2}' meas.log > metric
    [ $rc = 0 ] && grep -q "^Totals" meas.log && ! grep -qiE "error|timeout" meas.log && echo pass > valid || echo FAIL > valid )
