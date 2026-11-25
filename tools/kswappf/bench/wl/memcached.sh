# memcached 1.6.29 (4 threads) replaying Twitter cluster34 requests 0-100M; server in 3938MB
# (as eden), replay client native. Trace = held-out requests 100M-200M at 3537MB (as eden).
# Metric = replay kreq/s. Valid = 0 mismatches and hits == native (48775658).
WL_DIR=/data1/shaurya/deku_memcached; WL_MEM=3938M; WL_MEM_TRACE=3537M; WL_METRIC=kreq_per_sec; WL_BETTER=higher
wl_run() ( cd $1; M=$WL_DIR; PORT=11299
    mc_up() { exec 3<>/dev/tcp/127.0.0.1/$PORT 2>/dev/null || return 1; printf 'version\r\n' >&3; read -t 2 -r l <&3; exec 3>&-; [[ $l == VERSION* ]]; }
    mc_up 2>/dev/null && { echo "port $PORT busy"; exit 1; }
    sh -c 'kill -STOP $$; exec '"$M/install/bin/memcached -p $PORT -U 0 -l 127.0.0.1 -t 4 -m 65536 -c 1024 -A" > server.log 2>&1 &
    spid=$!; sleep 0.3; target_start $spid
    for _ in $(seq 1 240); do mc_up 2>/dev/null && break; sleep 0.5; done
    if [ "${MODE_TRACE:-0}" = 1 ]; then range="100000000 100000000"; else range="100000000"; fi
    { echo "server (in cgroup): $M/install/bin/memcached -p $PORT -U 0 -l 127.0.0.1 -t 4 -m 65536 -c 1024 -A"
      echo "measured (native client): $M/client/replay $M/traces/c34_300m.bin 127.0.0.1 $PORT 4 $range"; } >> cmd
    measure_begin
    $M/client/replay $M/traces/c34_300m.bin 127.0.0.1 $PORT 4 $range > replay.out 2> replay.err || true
    measure_end
    printf 'shutdown\r\n' > /dev/tcp/127.0.0.1/$PORT 2>/dev/null || true
    for _ in $(seq 1 60); do kill -0 $spid 2>/dev/null || break; sleep 1; done
    kill -9 $spid 2>/dev/null || true; wait $spid 2>/dev/null || true
    read -r _ reqs _ gets _ hits _ mism _ secs _ kps < replay.out || true
    echo ${kps:-0} > metric
    [ "${MODE_TRACE:-0}" = 1 ] && { echo n/a > valid; exit 0; }
    [ "${mism:-x}" = 0 ] && [ "${hits:-x}" = 48775658 ] && echo pass > valid || echo FAIL > valid )
