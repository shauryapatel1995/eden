# GAPBS sssp on lj.wsg (training input, 20 trials from different sources; trees for gap_sssp), 20 trial(s), OMP_NUM_THREADS=16.
# Metric = GAPBS "Average Time" (the kernel trials, graph loading excluded); elapsed_s = whole process.
# Valid = -a stats vs native (wl/gap_check.py).
WL_DIR=/data1/shaurya/deku_gap_sssp_lj; WL_MEM=${WL_MEM_OVERRIDE:-596M}; WL_METRIC=avg_trial_secs; WL_BETTER=lower   # 50% of native anon peak (1191MB)
wl_run() ( cd $1; export OMP_NUM_THREADS=16
    run_single "/data1/shaurya/deku_gapbs/gapbs/sssp -f /data1/shaurya/deku_gapbs/gapbs/benchmark/graphs/lj.wsg -n20 -a -d2"
    awk '/^Average Time:/ {print $3}' stdout.out > metric
    if [ "${MODE_NATIVE:-0}" = 1 ]; then echo pass > valid; exit 0; fi
    python3 -I /data1/shaurya/linux66_bench/wl/gap_check.py stdout.out $WL_DIR/linux66b/native/stdout.out > valid )
