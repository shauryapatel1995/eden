# SPEC 520.omnetpp_r, train input, 111MB (as eden). Trace = same input (104M faults); trees are
# trained on its first 3M faults (as eden's omnetpp_3m trees). Valid = specdiff on General-0.sca.
WL_DIR=/data1/shaurya/deku_omnetpp; WL_MEM=111M; WL_METRIC=wall_secs; WL_BETTER=lower; WL_TRAIN_FAULTS=3000000
X=/data1/shaurya/spec2017/omnetpp_build
wl_prepare() { cp -r $X/train/omnetpp.ini $X/train/ned $1/; }
wl_run() ( cd $1; run_single "$X/omnetpp_r -c General -r 0"; spec_env
    specdiff_ok -a 0.000001 -r 0.00001 -l 10 --floatcompare $X/train/expected/General-0.sca General-0.sca && echo pass > valid || echo FAIL > valid )
