# SPEC 505.mcf_r, refrate input (inp_refrate.in), 306MB = 50% of native peak (611MB). Trees = the mcf (train-input) trees,
# symlinked from /data1/shaurya/deku_mcf/linux66b. Valid = stdout + mcf.out == native refrate.
WL_DIR=/data1/shaurya/deku_mcf_ref; WL_MEM=${WL_MEM_OVERRIDE:-306M}; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; X=/data1/shaurya/spec2017/mcf_build; R=$WL_DIR/linux66b/native
    run_single "$X/mcf $X/inp_refrate.in"
    if [ "${MODE_NATIVE:-0}" = 1 ]; then echo pass > valid; exit 0; fi
    cmp -s stdout.out $R/stdout.out && cmp -s mcf.out $R/mcf.out && echo pass > valid || echo FAIL > valid
    rm -f mcf.out )
