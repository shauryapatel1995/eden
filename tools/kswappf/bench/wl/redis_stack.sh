# Full DiLOS-exact Redis LRANGE (as deku_redis/linux66_stack/run_ssd.sh): Redis 6.2.4 MALLOC=libc + DiLOS
# redis.conf (port 6392), populate random_lpush -d 1024 -n 20000000 -r 100000 (~22GB, not measured),
# measure random_lrange_600 -d 1024 -n 100000 -r 100000. Deku = stack + spatial trees (st_ext4):
# eden_swappf_ext4_d5_200k.ko spatial=1 stack=1 next_n=4 (trained on the scaled Linux trace).
# Metric = LRANGE phase seconds (redis-benchmark "requests completed in"). Valid = all 100000 requests completed.
WL_DIR=/data1/shaurya/deku_redis_stack; WL_MEM=${WL_MEM_OVERRIDE:-2560M}; WL_METRIC=lrange_secs; WL_BETTER=lower
KO_ARGS_st_ext4="spatial=1 stack=1 next_n=4"
wl_run() ( cd $1; RD=/data1/shaurya/deku_redis/dilos_exact; R=/data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src
    cli() { $R/redis-cli -p 6392 "$@"; }
    cli ping >/dev/null 2>&1 && { echo "port 6392 busy"; exit 1; }
    sh -c 'kill -STOP $$; exec '"$RD/redis-6.2.4/src/redis-server $RD/redis.conf --pidfile $1/redis.pid" > server.log 2>&1 &
    spid=$!; sleep 0.3; target_start $spid
    { echo "server (in cgroup, cwd $1): $RD/redis-6.2.4/src/redis-server $RD/redis.conf"
      echo "load (native): $R/redis-benchmark -p 6392 -t random_lpush -d 1024 -n 20000000 -r 100000"
      echo "measured (native): $R/redis-benchmark -p 6392 -t random_lrange_600 -d 1024 -n 100000 -r 100000"; } >> cmd
    for _ in $(seq 1 120); do cli ping 2>/dev/null | grep -q PONG && break; sleep 0.5; done
    $R/redis-benchmark -p 6392 -t random_lpush -d 1024 -n 20000000 -r 100000 > populate.log 2>&1
    measure_begin
    $R/redis-benchmark -p 6392 -t random_lrange_600 -d 1024 -n 100000 -r 100000 > lrange.log 2>&1
    measure_end
    cli shutdown nosave >/dev/null 2>&1 || true; wait $spid || true
    tr '\r' '\n' < lrange.log | grep "requests completed in" | tail -1 | awk '{print $5}' > metric
    tr '\r' '\n' < lrange.log | grep -q "100000 requests completed" && echo pass > valid || echo FAIL > valid )
