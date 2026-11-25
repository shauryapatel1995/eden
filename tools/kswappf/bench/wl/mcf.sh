# SPEC 505.mcf_r, train input, 273MB (as eden). Trace = same input. Valid = stdout + mcf.out == native.
WL_DIR=/data1/shaurya/deku_mcf; WL_MEM=273M; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; X=/data1/shaurya/spec2017/mcf_build; R=$WL_DIR/linux66/native
    run_single "$X/mcf $X/inp_train.in"
    cmp -s stdout.out $R/stdout.txt && cmp -s mcf.out $R/mcf.out && echo pass > valid || echo FAIL > valid
    rm -f mcf.out )
