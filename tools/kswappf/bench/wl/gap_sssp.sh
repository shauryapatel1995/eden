# GAPBS sssp on twitter.wsg (test input; trees = gap_sssp_lj's, trained on LiveJournal), 1 trial(s), OMP_NUM_THREADS=16.
# Metric = GAPBS "Average Time" (the kernel trials, graph loading excluded); elapsed_s = whole process.
# Valid = -a stats vs native (wl/gap_check.py).
WL_DIR=/data1/shaurya/deku_gap_sssp; WL_MEM=${WL_MEM_OVERRIDE:-11998M}; WL_METRIC=avg_trial_secs; WL_BETTER=lower   # 50% of native anon peak (23.4GB)
wl_run() ( cd $1; export OMP_NUM_THREADS=16
    run_single "/data1/shaurya/deku_gapbs/gapbs/sssp -f /data1/shaurya/deku_gapbs/gapbs/benchmark/graphs/twitter.wsg -n1 -a -d2"
    awk '/^Average Time:/ {print $3}' stdout.out > metric
    if [ "${MODE_NATIVE:-0}" = 1 ]; then echo pass > valid; exit 0; fi
    python3 -I /data1/shaurya/linux66_bench/wl/gap_check.py stdout.out $WL_DIR/linux66b/native/stdout.out > valid )
