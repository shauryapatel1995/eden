# Webservice (AIFM port), 5M requests, 13391MB (as eden). Trace = 1M requests, seed 1 (as eden).
# Valid = checksum == native.
WL_DIR=/data1/shaurya/deku_webservice; WL_MEM=13391M; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; A=$WL_DIR/app/webservice
    if [ "${MODE_TRACE:-0}" = 1 ]; then run_single "$A 1000000 27 2097152 1"; else run_single "$A 5000000"; fi
    mv stdout.out out.txt
    [ "${MODE_TRACE:-0}" = 1 ] && { echo n/a > valid; exit 0; }
    [ "$(grep checksum out.txt)" = "checksum: b100ad7b383bfdd3" ] && echo pass > valid || echo FAIL > valid )
