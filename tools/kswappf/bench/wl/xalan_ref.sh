# SPEC 523.xalancbmk_r, refrate input (t5.xml), 240MB = 50% of native peak (480MB).
# Trees = xalan_train (train-input) trees, symlinked from /data1/shaurya/deku_xalan_train/linux66b.
# Valid = specdiff vs ref-t5.out.
WL_DIR=/data1/shaurya/deku_xalan_ref; WL_MEM=240M; WL_METRIC=wall_secs; WL_BETTER=lower
X=/data1/shaurya/spec2017/xalan_build
wl_prepare() { for f in t5.xml xalanc.xsl 100mb.xsd ref.lst; do ln -s $X/refrate/$f $1/$f; done; }
wl_run() ( cd $1; run_single "$X/cpuxalan_r -v t5.xml xalanc.xsl"; spec_env
    specdiff_ok -l 10 $X/refrate/expected/ref-t5.out stdout.out && echo pass > valid || echo FAIL > valid )
