# WiredTiger YCSB-C (100% Zipfian point reads; app/ycsb_c.c, same load as ycsb_e), in-memory WT,
# test 10M records / 20M reads. Trace = different input (8M records, 10M reads, cache 32, seed 7).
# Memory = 50% of native peak: test 3667M (7.33GB), trace 2777M (5.42GB). Valid = checksum == native.
WL_DIR=/data1/shaurya/deku_wt_c; WL_MEM=${WL_MEM_OVERRIDE:-3667M}; WL_MEM_TRACE=${WL_MEM_TRACE_OVERRIDE:-2777M}; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; A=/data1/shaurya/deku_wiredtiger/app/ycsb_c
    if [ "${MODE_TRACE:-0}" = 1 ]; then run_single "$A 8000000 10000000 32 7"; else run_single "$A 10000000 20000000"; fi
    mv stdout.out out.txt
    [ "${MODE_TRACE:-0}" = 1 ] && { echo n/a > valid; exit 0; }
    [ "${MODE_NATIVE:-0}" = 1 ] && { echo pass > valid; exit 0; }
    [ "$(grep checksum out.txt)" = "$(grep checksum $WL_DIR/linux66b/native/out.txt)" ] && echo pass > valid || echo FAIL > valid )
