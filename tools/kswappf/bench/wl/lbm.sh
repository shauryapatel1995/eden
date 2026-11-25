# SPEC 519.lbm_r, train input (300 steps, 100x100x130 grid). Trace = same input (trees for lbm_ref).
# Valid = specdiff -a 1e-7 (SPEC's abstol) vs SPEC's train lbm.out.
WL_DIR=/data1/shaurya/deku_lbm; WL_MEM=${WL_MEM_OVERRIDE:-205M}; WL_METRIC=wall_secs; WL_BETTER=lower   # 50% of native peak (410MB, train and ref)
# Train on the first 3M of the trace's 26.4M faults (~34 of 300 identical-pattern steps).
WL_TRAIN_FAULTS=3000000
LBM=/data1/shaurya/spec2017/lbm_build; LBM_IN=$LBM; LBM_OUT=/data1/shaurya/spec2017/benchspec/CPU/519.lbm_r/data/train/output/lbm.out
wl_prepare() { cp $LBM_IN/*.of $1/; }
wl_run() ( cd $1
    run_single "$LBM/lbm $(cat $LBM_IN/lbm.in)"
    spec_env; specdiff_ok -a 1e-7 stdout.out $LBM_OUT && echo pass > valid || echo FAIL > valid
    rm -f *.of )
