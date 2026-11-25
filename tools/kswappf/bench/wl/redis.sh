# Redis "mid": 2M random_lpush (1KB) build, then 100k random_lrange_600 measured; server in
# 1206MB (as eden), clients native. Trace = same. Metric = LRANGE req/s. No output check
# (valid = benchmark completed all requests).
WL_DIR=/data1/shaurya/deku_redis; WL_MEM=1206M; WL_METRIC=lrange_rps; WL_BETTER=higher
wl_run() ( cd $1; R=$WL_DIR/dilos-artifact/tools/redis-seq/src
    cli() { $R/redis-cli -p 6390 "$@"; }
    cli ping >/dev/null 2>&1 && { echo "port 6390 busy"; exit 1; }
    sh -c 'kill -STOP $$; cd '$R'; exec ./redis-server --port 6390 --save "" --appendonly no' > server.log 2>&1 &
    spid=$!; sleep 0.3; target_start $spid
    { echo "server (in cgroup): cd $R; ./redis-server --port 6390 --save '' --appendonly no"
      echo "load (native): ./redis-benchmark -p 6390 -t random_lpush -d 1024 -n 2000000 -r 100000"
      echo "measured (native): ./redis-benchmark -p 6390 -t random_lrange_600 -d 1024 -n 100000 -r 100000"; } >> cmd
    for _ in $(seq 1 120); do cli ping 2>/dev/null | grep -q PONG && break; sleep 0.5; done
    (cd $R && ./redis-benchmark -p 6390 -t random_lpush -d 1024 -n 2000000 -r 100000) > lpush.log 2>&1
    measure_begin
    (cd $R && ./redis-benchmark -p 6390 -t random_lrange_600 -d 1024 -n 100000 -r 100000) > lrange.log 2>&1
    measure_end
    cli shutdown nosave >/dev/null 2>&1 || true; wait $spid || true
    tr '\r' '\n' < lrange.log | grep "requests per second" | tail -1 | awk '{print $1}' > metric
    tr '\r' '\n' < lrange.log | grep -q "100000 requests completed" && echo pass > valid || echo FAIL > valid )
