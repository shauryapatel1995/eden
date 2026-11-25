# Dataframe (NYC taxi), 16.6GB test CSV, 15872MB (as eden). Trace = held-out month
# train_2016-07.csv at 1437MB (as eden). Valid = results == native reference.
# The app reads csv/all.csv and writes out/*.csv at absolute paths.
WL_DIR=/data1/shaurya/deku_dataframe; WL_MEM=15872M; WL_MEM_TRACE=1437M; WL_METRIC=wall_secs; WL_BETTER=lower
wl_run() ( cd $1; C=$WL_DIR/csv
    if [ "${MODE_TRACE:-0}" = 1 ]; then ln -sfn train_2016-07.csv $C/all.csv; fi
    run_single "$WL_DIR/app/build/bin/main"
    ln -sfn test_all.csv $C/all.csv
    [ "${MODE_TRACE:-0}" = 1 ] && { echo n/a > valid; exit 0; }
    diff -q <(grep -vE "^Step [0-9]+: [0-9]+ us$|^Total: [0-9]+ us$" <(tr -d '\0' < stdout.out)) $WL_DIR/native/reference_results.txt >/dev/null && echo pass > valid || echo FAIL > valid )
