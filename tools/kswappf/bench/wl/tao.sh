# TaoBench TEST workload: memsize 6 (4.5 GiB cache), 600 s warmup, 120 s measured; trees from
# tao_mini_train (DCPerf Mini settings), symlinked. See wl/tao_common.sh.
. /data1/shaurya/linux66_bench/wl/tao_common.sh
WL_DIR=/data1/shaurya/deku_tao; WL_MEM=${WL_MEM_OVERRIDE:-3477M}; WL_METRIC=ops_per_sec; WL_BETTER=higher   # 50% of native anon peak (7293480960 B)
wl_run() { tao_run $1 6 600 120; }
