# XGBoost training (FarSight v2 setup), NYC taxi Jan-Jun 2016 first 73M rows x 17 features
# (next 1M held out), label tip > 0, 20 rounds, hist, depth 8, 1 thread. Memory = 50% of
# native peak RSS (18.9GB -> 9676M; memory.peak 24.5GB includes the input page cache). Trees = xgb_fs_adult (trained on the Adult Census trace only), symlinked.
# Valid = final test-auc/logloss line == native.
WL_DIR=/data1/shaurya/deku_xgb_farsight/taxi; WL_MEM=${WL_MEM_OVERRIDE:-9676M}; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; F=/data1/shaurya/deku_xgb_farsight
    run_single "$F/app/xgb_train $F/data_taxi 20 1"
    mv stdout.out out.txt
    [ "${MODE_NATIVE:-0}" = 1 ] && { cp out.txt $WL_DIR/native_out.txt; echo pass > valid; exit 0; }
    [ "$(grep final out.txt)" = "$(grep final $WL_DIR/native_out.txt)" ] && echo pass > valid || echo FAIL > valid )
