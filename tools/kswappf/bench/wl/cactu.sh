# SPEC 507.cactuBSSN_r, train input, 53MB (as eden). Trace = same input. Valid = specdiff vs SPEC expected.
WL_DIR=/data1/shaurya/deku_cactu; WL_MEM=53M; WL_METRIC=wall_secs; WL_BETTER=lower
X=/data1/shaurya/spec2017/cactu_build
wl_prepare() { cp $X/train/spec_train.par $1/; }
wl_run() ( cd $1; run_single "$X/cactusBSSN_r spec_train.par"; spec_env; v=pass
    for f in gxx.xl gxy.xl; do specdiff_ok -a 5e-13 -l 10 --floatcompare $X/train/expected/$f $f || v=FAIL; done
    specdiff_ok -l 10 --floatcompare $X/train/expected/spec_train.out stdout.out || v=FAIL
    echo $v > valid; [ $v = pass ] && rm -f gxx.xl gxy.xl; true )
