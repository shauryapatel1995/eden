# Full DiLOS-exact Redis LRANGE (as deku_redis/linux66_stack/run_ssd.sh): Redis 6.2.4 MALLOC=libc + DiLOS
# redis.conf (port 6392), populate random_lpush -d 1024 -n 20000000 -r 100000 (~22GB, not measured),
# measure random_lrange_600 -d 1024 -n 100000 -r 100000. Deku = stack + spatial trees (st_ext4):
# eden_swappf_ext4_d5_200k.ko spatial=1 stack=1 next_n=4 (trained on the scaled Linux trace).
# redis_p99: same as redis_stack.sh, but the measured LRANGE phase uses a copy of the same redis-benchmark
# (/data1/shaurya/deku_redis_p99/redis-seq, only change: also prints exact per-request latency percentiles in us).
# Metric = LRANGE p99 request latency, us ("latency_us ... p99"); lrange_secs also saved. Valid = all 100000 completed.
WL_DIR=/data1/shaurya/deku_redis_p99; WL_MEM=${WL_MEM_OVERRIDE:-2560M}; WL_METRIC=lrange_p99_us; WL_BETTER=lower
KO_ARGS_st_ext4="spatial=1 stack=1 next_n=4"
wl_run() ( cd $1; RD=/data1/shaurya/deku_redis/dilos_exact; R=/data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src; RB=/data1/shaurya/deku_redis_p99/redis-seq/src
    cli() { $R/redis-cli -p 6392 "$@"; }
    cli ping >/dev/null 2>&1 && { echo "port 6392 busy"; exit 1; }
    sh -c 'kill -STOP $$; exec '"$RD/redis-6.2.4/src/redis-server $RD/redis.conf --pidfile $1/redis.pid" > server.log 2>&1 &
    spid=$!; sleep 0.3; target_start $spid
    { echo "server (in cgroup, cwd $1): $RD/redis-6.2.4/src/redis-server $RD/redis.conf"
      echo "load (native): $R/redis-benchmark -p 6392 -t random_lpush -d 1024 -n 20000000 -r 100000"
      echo "measured (native): $RB/redis-benchmark -p 6392 -t random_lrange_600 -d 1024 -n 100000 -r 100000"; } >> cmd
    for _ in $(seq 1 120); do cli ping 2>/dev/null | grep -q PONG && break; sleep 0.5; done
    $R/redis-benchmark -p 6392 -t random_lpush -d 1024 -n 20000000 -r 100000 > populate.log 2>&1
    measure_begin
    $RB/redis-benchmark -p 6392 -t random_lrange_600 -d 1024 -n 100000 -r 100000 > lrange.log 2>&1
    measure_end
    cli shutdown nosave >/dev/null 2>&1 || true; wait $spid || true
    tr '\r' '\n' < lrange.log | grep "requests completed in" | tail -1 | awk '{print $5}' > lrange_secs
    tr '\r' '\n' < lrange.log | grep "^latency_us" | tail -1 | awk '{print $9}' > metric
    tr '\r' '\n' < lrange.log | grep -q "100000 requests completed" && echo pass > valid || echo FAIL > valid )
