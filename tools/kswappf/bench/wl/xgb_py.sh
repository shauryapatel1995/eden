# XGBoost training with the Python package (FarSight v2 setup), NYC taxi Jan-Jun 2016 first
# 73M rows x 17 features (next 1M held out), label tip > 0, 100 rounds, hist, depth 8,
# 28 threads. Memory 9638M = 50% of native peak RSS (18.8GB). Trees = xgb_py_adult (trained
# on the Adult Census trace only), symlinked. Valid = final test auc/logloss == native.
WL_DIR=/data1/shaurya/deku_xgb_farsight/taxi_py; WL_MEM=${WL_MEM_OVERRIDE:-9638M}; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; F=/data1/shaurya/deku_xgb_farsight
    run_single "python3 $F/app_py/xgb_train.py $F/data_taxi 100 28"
    mv stdout.out out.txt
    [ "${MODE_NATIVE:-0}" = 1 ] && { cp out.txt $WL_DIR/native_out.txt; echo pass > valid; exit 0; }
    [ "$(grep final out.txt)" = "$(grep final $WL_DIR/native_out.txt)" ] && echo pass > valid || echo FAIL > valid )
