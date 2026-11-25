# SPEC 507.cactuBSSN_r, refrate input (spec_ref.par), 395MB = 50% of native peak (790MB).
# Trees = the cactu (train-input) trees, symlinked from /data1/shaurya/deku_cactu/linux66b.
# Valid = specdiff vs SPEC refrate expected.
WL_DIR=/data1/shaurya/deku_cactu_ref; WL_MEM=395M; WL_METRIC=wall_secs; WL_BETTER=lower
X=/data1/shaurya/spec2017/cactu_build
wl_prepare() { cp $X/refrate/spec_ref.par $1/; }
wl_run() ( cd $1; run_single "$X/cactusBSSN_r spec_ref.par"; spec_env; v=pass
    for f in gxx.xl gxy.xl; do specdiff_ok -a 5e-13 -l 10 --floatcompare $X/refrate/expected/$f $f || v=FAIL; done
    specdiff_ok -l 10 --floatcompare $X/refrate/expected/spec_ref.out stdout.out || v=FAIL
    echo $v > valid; [ $v = pass ] && rm -f gxx.xl gxy.xl; true )
