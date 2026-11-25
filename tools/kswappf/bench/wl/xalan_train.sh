# SPEC 523.xalancbmk_r, train input (allbooks.xml) - trace + trees only. Memory 219MB = 50% of
# native train peak (438MB). Valid = specdiff vs train expected.
WL_DIR=/data1/shaurya/deku_xalan_train; WL_MEM=${WL_MEM_OVERRIDE:-219M}; WL_METRIC=wall_secs; WL_BETTER=lower
X=/data1/shaurya/spec2017/xalan_build
wl_prepare() { for f in allbooks.xml xalanc.xsl train.xsd train.lst; do ln -s $X/train/$f $1/$f; done; }
wl_run() ( cd $1; run_single "$X/cpuxalan_r -v allbooks.xml xalanc.xsl"; spec_env
    specdiff_ok -l 10 $X/train/expected/train-allbooks.out stdout.out && echo pass > valid || echo FAIL > valid )
