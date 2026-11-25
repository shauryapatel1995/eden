# TaoBench (DCPerf, facebookresearch/DCPerf packages/tao_bench) server + clients on one host, as
# DCPerf's standalone mode, with the server command and client command built exactly as
# packages/tao_bench/run.py does (64 cores: 48 fast threads, 12 dispatchers, 144 slow threads;
# 2 clients x 58 threads; TLS on; 8 KB values; GET-only, uniform random keys sized for a 0.9
# hit ratio; --randomize, so every run has a different request stream). Deviation: no
# memory file (-e /dev/shm/...): the cache must be anonymous memory to reach our swap-in hook.
# Server in the cgroup; clients native. Warmup (clients run --test-time=WARMUP), then the
# measured phase (clients run --test-time=TEST). Metric = DCPerf's server-side score (mean fast_qps + slow_qps over
# the measured phase's 1-s server stats lines). Valid = both clients exited 0 with a Totals line, server alive.
TB=/data1/shaurya/dcperf_src/DCPerf/benchmarks/tao_bench
tao_run() ( cd $1; MEMSIZE=$2; WARMUP=$3; TEST=$4; PORT=11411
    NMEM=$(python3 -c "print(int($MEMSIZE*1024*0.75))")
    KMAX=$(python3 -c "print(int(int(int($MEMSIZE*1024*0.75)*1024*1024/434)/0.9*0.807))")
    OPTS="lru_crawler,tao_it_gen_file=$TB/leader_sizes.json,tao_max_item_size=65536,tao_gen_payload=0,tao_slow_dispatchers=12,tao_num_slow_threads=144,tao_max_slow_reqs=1024,tao_worker_sleep_ns=100,tao_dispatcher_sleep_ns=100,tao_slow_sleep_ns=100,tao_slow_path_sleep_us=0,tao_compress_items=1,tao_stats_sleep_ms=1000,tao_slow_use_semaphore=1,tao_pin_threads=0,tao_smart_nanosleep=0,ssl_chain_cert=/data1/shaurya/deku_tao_certs/example.crt,ssl_key=/data1/shaurya/deku_tao_certs/example.key"
    srv="$TB/tao_bench_server -c 180000 -u nobody -m $NMEM -t 48 -B binary -p $PORT -I 16m -Z -o $OPTS"
    cli() { echo "$TB/tao_bench_client -s 127.0.0.1 -p $PORT -P memcache_binary --key-pattern=R:R --distinct-client-seed --randomize -R --hide-histogram --expiry-range=1800-1802 --data-size-range=8191-8193 --ratio=0:1 --key-minimum=1 --key-maximum=$KMAX -t 58 --clients=190 --threads-coherence=0 --clients-coherence=3 --key-bytes=220 --test-time=$1 --cert=/data1/shaurya/deku_tao_certs/example.crt --key=/data1/shaurya/deku_tao_certs/example.key --tls --tls-skip-verify"; }
    { echo "server (in cgroup): LD_LIBRARY_PATH=$TB/build-deps/lib $srv"
      echo "clients (native, x2, warmup): LD_LIBRARY_PATH=$TB/build-deps/lib $(cli $WARMUP)"
      echo "clients (native, x2, measured): LD_LIBRARY_PATH=$TB/build-deps/lib $(cli $TEST)"; } >> cmd
    export LD_LIBRARY_PATH=$TB/build-deps/lib
    sh -c 'kill -STOP $$; exec '"$srv" > server.log 2>&1 &
    spid=$!; sleep 0.3; target_start $spid
    for _ in $(seq 1 120); do (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break; sleep 0.5; done
    $(cli $WARMUP) > warmup1.log 2>&1 & w1=$!; $(cli $WARMUP) > warmup2.log 2>&1 & w2=$!; wait $w1 $w2
    # a swapping server closes the warmup clients' ~22k connections slowly; with them still open
    # the measured clients run out of local ports (32768-60999), so wait until it has let go of them
    for _ in $(seq 1 1800); do
        c=$(grep -o 'crnt_conn = [0-9]*' server.log | tail -1 | awk '{print $3}')
        [ -n "$c" ] && [ "$c" -lt 1000 ] && break; sleep 1
    done
    echo "server connections before the measured phase: $c" >> cmd
    measure_begin; l0=$(wc -l < server.log)
    [ "${MODE_TRACE:-0}" = 1 ] && echo MEASURE_BEGIN | sudo tee /sys/kernel/tracing/trace_marker >/dev/null
    $(cli $TEST) > meas1.log 2>&1 & m1=$!; $(cli $TEST) > meas2.log 2>&1 & m2=$!
    rc=0; wait $m1 || rc=1; wait $m2 || rc=1
    measure_end; l1=$(wc -l < server.log)
    alive=0; kill -0 $spid 2>/dev/null && alive=1
    kill $spid 2>/dev/null || true; sleep 2; kill -9 $spid 2>/dev/null || true; wait $spid 2>/dev/null || true
    # DCPerf's score (packages/tao_bench/parser.py): mean over the server's 1-s stats lines of
    # fast_qps + slow_qps, here over the lines written during the measured phase; hit ratio likewise.
    # (Client Totals are not used: under swap the clients count far more GETs than the server serves.)
    sed -n "$((l0+1)),${l1}p" server.log | grep -o 'fast_qps = [0-9.]*, hit_rate = [0-9.]*, slow_qps = [0-9.]*' |
        awk -F'[=,]' '$2+0>1 {q+=$2+$6; f+=$2; n++} END {if (n) {printf "%.1f\n", q/n > "metric"; printf "%.4f\n", f/q > "hit_ratio"}}'
    awk '/^Totals/ {s+=$2} END {print s+0}' meas1.log meas2.log > client_ops_per_sec
    [ $rc = 0 ] && [ $alive = 1 ] && [ -s metric ] && echo pass > valid || echo FAIL > valid )
