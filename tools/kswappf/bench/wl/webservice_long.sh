# Webservice (AIFM port), 25M requests: long-running variant of webservice for the senpai experiment
# (same binary, defaults and trees as webservice; its own dir so native/ is not overwritten).
# Valid = checksum == this variant's native run (native.sh webservice_long).
WL_DIR=/data1/shaurya/deku_webservice_long; WL_MEM=13391M; WL_METRIC=wall_secs; WL_BETTER=lower
WS_REQUESTS=${WS_REQUESTS:-25000000}
wl_run() ( cd $1; A=/data1/shaurya/deku_webservice/app/webservice
    run_single "$A $WS_REQUESTS"
    mv stdout.out out.txt
    [ "${MODE_NATIVE:-0}" = 1 ] && { echo pass > valid; exit 0; }
    [ "$(grep checksum out.txt)" = "$(grep checksum $WL_DIR/linux66b/native/out.txt)" ] && echo pass > valid || echo FAIL > valid )
