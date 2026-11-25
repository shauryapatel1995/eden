# GAPBS pr on twitter.sg (test input; trees = gap_pr_lj's, trained on LiveJournal), 1 trial(s), OMP_NUM_THREADS=16.
# Metric = GAPBS "Average Time" (the kernel trials, graph loading excluded); elapsed_s = whole process.
# Valid = -a stats vs native (wl/gap_check.py).
WL_DIR=/data1/shaurya/deku_gap_pr; WL_MEM=${WL_MEM_OVERRIDE:-6424M}; WL_METRIC=avg_trial_secs; WL_BETTER=lower   # 50% of native anon peak (12.55GB)
wl_run() ( cd $1; export OMP_NUM_THREADS=16
    run_single "/data1/shaurya/deku_gapbs/gapbs/pr -f /data1/shaurya/deku_gapbs/gapbs/benchmark/graphs/twitter.sg -n1 -a "
    awk '/^Average Time:/ {print $3}' stdout.out > metric
    if [ "${MODE_NATIVE:-0}" = 1 ]; then echo pass > valid; exit 0; fi
    python3 -I /data1/shaurya/linux66_bench/wl/gap_check.py stdout.out $WL_DIR/linux66b/native/stdout.out > valid )
