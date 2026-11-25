# XGBoost training app, 100 rounds, 1 thread, 1844MB (as eden). Trace = 20 rounds (eden had
# no trace). Valid = final test-auc/logloss line == native.
WL_DIR=/data1/shaurya/deku_xgbapp; WL_MEM=1844M; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; A=$WL_DIR/app/xgb_train
    if [ "${MODE_TRACE:-0}" = 1 ]; then run_single "$A $WL_DIR/data 20 1"; else run_single "$A $WL_DIR/data 100 1"; fi
    mv stdout.out out.txt
    [ "${MODE_TRACE:-0}" = 1 ] && { echo n/a > valid; exit 0; }
    [ "$(grep final out.txt)" = "$(grep final $WL_DIR/native/out100.txt)" ] && echo pass > valid || echo FAIL > valid )
