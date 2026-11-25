# WiredTiger YCSB-E, 10M records / 10M ops, 3668MB (as eden). Trace = eden training input
# (8M records, 5M ops, cache 32, seed 7) at 3150MB. Valid = checksum == native.
WL_DIR=/data1/shaurya/deku_wiredtiger; WL_MEM=3668M; WL_MEM_TRACE=3150M; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; A=$WL_DIR/app/ycsb_e
    if [ "${MODE_TRACE:-0}" = 1 ]; then run_single "$A 8000000 5000000 32 7"; else run_single "$A 10000000 10000000"; fi
    mv stdout.out out.txt
    [ "${MODE_TRACE:-0}" = 1 ] && { echo n/a > valid; exit 0; }
    [ "$(grep checksum out.txt)" = "checksum: 8d09ae4a6394c3a3" ] && echo pass > valid || echo FAIL > valid )
