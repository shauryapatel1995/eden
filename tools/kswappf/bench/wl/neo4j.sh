# Neo4j 4.4 Community + GDS 2.6.8 PageRank on KONECT Baidu (2.1M nodes, 17.8M edges), as in
# Canvas (NSDI'23). Server JVM (4g heap, 1g page cache, no pre-touch, -XX:-UseContainerSupport) in WL_MEM = half its
# native cgroup peak (3551MB -> 1776MB); cypher-shell native. Measured: graph projection + 20 PageRank iterations.
# Trace = same job. Valid = 20 iterations and PageRank mean == native.
WL_DIR=/data1/shaurya/deku_neo4j; WL_MEM=1776M; WL_METRIC=job_secs; WL_BETTER=lower
wl_run() ( cd $1; N=$WL_DIR/neo4j-community-4.4.44
    cs() { $N/bin/cypher-shell -a bolt://127.0.0.1:7687 --format plain "$@"; }
    cs "RETURN 1" >/dev/null 2>&1 && { echo "neo4j already up"; exit 1; }
    sh -c 'kill -STOP $$; exec '"$N/bin/neo4j console" > server.log 2>&1 &
    bpid=$!; sleep 0.3; target_start $bpid
    # the bootstrap JVM forks the server JVM: that is the process to trace / prefetch for
    for _ in $(seq 1 200); do spid=$(pgrep -P $bpid -f CommunityEntryPoint) && break; sleep 0.1; done
    set_target $spid
    for _ in $(seq 1 300); do cs "RETURN 1" >/dev/null 2>&1 && break; sleep 0.5; done
    { echo "server (in cgroup, target = server JVM child): $N/bin/neo4j console  (conf: $N/conf/neo4j.conf)"
      echo "measured (native): $N/bin/cypher-shell -a bolt://127.0.0.1:7687 --format plain -f $WL_DIR/pagerank.cypher"; } >> cmd
    measure_begin
    cs -f $WL_DIR/pagerank.cypher > job.out 2> job.err || true
    measure_end
    kill $spid 2>/dev/null || true
    for _ in $(seq 1 120); do kill -0 $bpid 2>/dev/null || break; sleep 0.5; done
    kill -9 $spid $bpid 2>/dev/null || true; wait $bpid 2>/dev/null || true
    echo "$(cat t_end) - $(cat t_begin)" | bc > metric
    [ "${MODE_TRACE:-0}" = 1 ] && { echo n/a > valid; exit 0; }
    line=$(grep -A1 "^ranIterations" job.out | tail -1)
    [ "$(echo $line | cut -d, -f1)" = 20 ] && python3 -c "import sys; sys.exit(abs(float('$(echo $line | cut -d, -f4)')/0.4406857111666341-1) > 1e-6)" \
        && echo pass > valid || echo FAIL > valid )
