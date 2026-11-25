# XGBoost training with the Python package (FarSight v2 setup), Adult Census (32,561 train
# rows x 14 features, adult.test held out), 100 rounds, hist, depth 8, 28 threads -
# trace + trees only. Memory 89M = 50% of native peak RSS (178MB). Valid = final test auc/logloss == native.
WL_DIR=/data1/shaurya/deku_xgb_farsight/adult_py; WL_MEM=${WL_MEM_OVERRIDE:-89M}; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; F=/data1/shaurya/deku_xgb_farsight
    run_single "python3 $F/app_py/xgb_train.py $F/data_adult 100 28"
    mv stdout.out out.txt
    [ "${MODE_NATIVE:-0}" = 1 ] && { cp out.txt $WL_DIR/native_out.txt; echo pass > valid; exit 0; }
    [ "$(grep final out.txt)" = "$(grep final $WL_DIR/native_out.txt)" ] && echo pass > valid || echo FAIL > valid )
