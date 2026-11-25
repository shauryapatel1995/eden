# TaoBench, DCPerf Mini settings (tao_bench_v1_mini_prep/tao_bench_v1_mini: memsize 3 -> 2.25 GiB
# cache, 300 s warmup, 10 s measured) -- the TRAINING workload for tao. See wl/tao_common.sh.
. /data1/shaurya/linux66_bench/wl/tao_common.sh
WL_DIR=/data1/shaurya/deku_tao_mini; WL_MEM=${WL_MEM_OVERRIDE:-2302M}; WL_METRIC=ops_per_sec; WL_BETTER=higher   # 50% of native anon peak (4828311552 B)
wl_run() { tao_run $1 3 300 10; }
