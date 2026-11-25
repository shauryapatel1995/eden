# GAPBS pr on twitter.sg, 150 trials (long-running variant of gap_pr for the senpai experiment;
# same binary, input and trees as gap_pr), OMP_NUM_THREADS=16.
# Metric = GAPBS "Average Time" over the 150 trials; elapsed_s = whole process.
# Valid = every trial's -a stats vs native (wl/gap_check.py).
WL_DIR=/data1/shaurya/deku_gap_pr; WL_MEM=${WL_MEM_OVERRIDE:-6424M}; WL_METRIC=avg_trial_secs; WL_BETTER=lower
GAP_TRIALS=${GAP_TRIALS:-150}
wl_run() ( cd $1; export OMP_NUM_THREADS=16
    run_single "/data1/shaurya/deku_gapbs/gapbs/pr -f /data1/shaurya/deku_gapbs/gapbs/benchmark/graphs/twitter.sg -n$GAP_TRIALS -a "
    awk '/^Average Time:/ {print $3}' stdout.out > metric
    python3 -I /data1/shaurya/linux66_bench/wl/gap_check.py stdout.out $WL_DIR/linux66b/native/stdout.out > valid )
