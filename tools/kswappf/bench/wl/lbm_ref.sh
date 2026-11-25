# SPEC 519.lbm_r, refrate input (3000 steps, same 100x100x130 grid). Trees = the lbm (train-input) trees, symlinked from /data1/shaurya/deku_lbm/linux66b.
# Valid = specdiff -a 1e-7 (SPEC's abstol) vs SPEC's refrate lbm.out.
WL_DIR=/data1/shaurya/deku_lbm_ref; WL_MEM=${WL_MEM_OVERRIDE:-205M}; WL_METRIC=wall_secs; WL_BETTER=lower   # 50% of native peak (410MB, train and ref)
LBM=/data1/shaurya/spec2017/lbm_build; LBM_IN=$LBM/ref; LBM_OUT=/data1/shaurya/spec2017/benchspec/CPU/519.lbm_r/data/refrate/output/lbm.out
wl_prepare() { cp $LBM_IN/*.of $1/; }
wl_run() ( cd $1
    run_single "$LBM/lbm $(cat $LBM_IN/lbm.in)"
    spec_env; specdiff_ok -a 1e-7 stdout.out $LBM_OUT && echo pass > valid || echo FAIL > valid
    rm -f *.of )
