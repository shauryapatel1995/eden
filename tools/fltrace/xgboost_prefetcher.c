/*
 * xgboost_prefetch.c - XGBoost prefetching policy
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* this whole file is a no-op unless built with DO_PREFETCH (see
 * `make fltrace.so DO_PREFETCH=1` in the top-level Makefile) - fltrace.so
 * is also used as a general page-fault profiling tool with no reason to
 * require xgboost, so we don't want the constructor below (which dlopen()s
 * libxgboost.so unconditionally) to run for every fltrace.so load */
#ifdef DO_PREFETCH

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>
#include <dlfcn.h>
#include "rmem/common.h"
#include "rmem/prefetch.h"
#include "native_prefetch_predict.h"

/* NATIVE_ONLY (opt-in: make fltrace.so DO_PREFETCH=1 NATIVE_ONLY=1) strips out
 * every bit of libxgboost usage below at COMPILE time, not just at runtime.
 * Runtime-only gating (checking EDEN_PREFETCH_NATIVE_MODEL and skipping the
 * libxgboost calls) still leaves libxgboost.so linked as a DT_NEEDED
 * dependency, which is enough for the dynamic linker to load it and run its
 * global C++ destructors (e.g. dmlc::Registry::~Registry()) at process exit
 * via _dl_fini() - and that destructor path itself can hang, even though we
 * never called a single xgboost function all run. Only actually not linking
 * the library avoids this. */
#ifndef NATIVE_ONLY

// XGBoost C API
#include <xgboost/c_api.h>

// Feature indices based on the training script
#define FEATURE_PC 0
#define FEATURE_OFFSET 1
#define FEATURE_DELTA 2
#define FEATURE_OFFSET_FROM_FAULTING 3
#define NUM_FEATURES 4

typedef struct {
    BoosterHandle booster;
    int is_loaded;
} XGBoostModel;

static XGBoostModel* global_model = NULL;
static pthread_mutex_t model_init_lock = PTHREAD_MUTEX_INITIALIZER;

#define XGBOOST_LIB_PATH_ENV "EDEN_PREFETCH_XGBOOST_LIB_PATH"
#define XGBOOST_LIB_PATH_DEFAULT "/usr/local/lib/libxgboost.so"
#define XGBOOST_MODEL_PATH_ENV "EDEN_PREFETCH_MODEL_PATH"
#define XGBOOST_MODEL_PATH_DEFAULT "./eden_xgboost_model.json"
#define NATIVE_MODEL_ENV "EDEN_PREFETCH_NATIVE_MODEL"

/* cached once - checked before doing anything that would touch libxgboost */
static int native_mode_enabled(void) {
    static int cached = -1;
    if (cached == -1) {
        const char *env = getenv(NATIVE_MODEL_ENV);
        cached = (env && env[0] == '1') ? 1 : 0;
    }
    return cached;
}

__attribute__((constructor))
void xgboost_pre_init(void) {
    /* native mode never calls into libxgboost at all, so don't even dlopen
     * it - see native_mode_enabled()'s callers for why this matters (a
     * starved thread pool from a one-time model load that's never used). */
    if (native_mode_enabled())
        return;

    // Attempt to open libxgboost.so. RTLD_NOW forces immediate resolution of all
    // symbols, and the act of dlopen() is what triggers the static constructors
    // of libxgboost.so to run.
    const char* lib_path = getenv(XGBOOST_LIB_PATH_ENV);
    if (!lib_path) lib_path = XGBOOST_LIB_PATH_DEFAULT;
    void *handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);

    if (!handle) {
        fprintf(stderr, "FATAL ERROR: Failed to explicitly dlopen and initialize libxgboost.so at %s\n", lib_path);
        fprintf(stderr, "dlerror: %s\n", dlerror());
        exit(1);
    }
    // dlclose(handle); // Do NOT close the handle if you intend to use the functions later.
}

/**
 * Initialize XGBoost model structure
 */
XGBoostModel* init_model() {
    xgboost_pre_init();
    XGBoostModel* model = (XGBoostModel*)malloc(sizeof(XGBoostModel));
    if (!model) {
        fprintf(stderr, "Failed to allocate memory for model\n");
        return NULL;
    }

    model->booster = NULL;
    model->is_loaded = 0;
    return model;
}

/**
 * Load trained XGBoost model from file
 */
int load_model(XGBoostModel* model, const char* model_path) {
    if (!model || !model_path) {
        fprintf(stderr, "Invalid model or path\n");
        return -1;
    }

    // First check if file exists and is readable
    FILE* test_file = fopen(model_path, "r");
    if (!test_file) {
        fprintf(stderr, "Error: Cannot open model file '%s'\n", model_path);
        perror("File error");
        return -1;
    }
    fclose(test_file);
    log_info("prefetch model file exists and is readable: %s", model_path);

    // Force XGBoost initialization
    int major, minor, patch;
    XGBoostVersion(&major, &minor, &patch);
    log_info("xgboost version: %d.%d.%d", major, minor, patch);

    // Create booster
    if (XGBoosterCreate(NULL, 0, &model->booster) != 0) {
        fprintf(stderr, "Failed to create XGBooster\n");
        const char* error_msg = XGBGetLastError();
        if (error_msg) {
            fprintf(stderr, "XGBoost error: %s\n", error_msg);
        }
        return -1;
    }

    // Force CPU-only: without this, the first prediction call lazily probes
    // for GPUs (xgboost::common::AllVisibleGPUs()), which under this
    // process's malloc/mmap interposition deadlocks on the handler thread
    // (self-recursive glibc __exit_funcs_lock via a CUDA-error-category
    // static's atexit registration) - a machine with no GPU/CUDA driver at
    // all still triggers this if the device probe isn't skipped upfront.
    if (XGBoosterSetParam(model->booster, "device", "cpu") != 0) {
        const char* error_msg = XGBGetLastError();
        fprintf(stderr, "Failed to force CPU device: %s\n",
            error_msg ? error_msg : "unknown error");
    }

    // Load model from file
    if (XGBoosterLoadModel(model->booster, model_path) != 0) {
        const char* error_msg = XGBGetLastError();
        if (error_msg) {
            fprintf(stderr, "XGBoost error: %s\n", error_msg);
        }
        fprintf(stderr, "Failed to load model from %s\n", model_path);
        XGBoosterFree(model->booster);
        model->booster = NULL;
        return -1;
    }

    model->is_loaded = 1;
    log_info("successfully loaded XGBoost model from %s", model_path);
    return 0;
}

/**
 * Preprocess features according to training script logic
 */
void preprocess_features(FeatureVector* features) {
    // Convert PC to categorical (in training, PC was converted to category)
    // For inference, we keep PC as-is since XGBoost handles categorical features

    // Scale delta by 4096 (as done in training)
    features->delta = features->delta / 4096.0f;
}

/* config for XGBoosterPredictFromDense: normal prediction, all trees, no
 * strict shape (matches the flat per-row output XGBoosterPredict used to
 * give us). "missing": NaN is the correct sentinel for our data - unlike
 * the old DMatrix path's "missing": -1, NaN can never collide with a real
 * feature value (the old -1 sentinel did: offset_from_faulting == -1 is a
 * legitimate value hit on nearly every fault, silently forcing that
 * feature to "missing" instead of using it) */
#define PREDICT_CONFIG_JSON \
    "{\"type\": 0, \"training\": false, \"iteration_begin\": 0, " \
    "\"iteration_end\": 0, \"missing\": NaN, \"strict_shape\": false}"

/**
 * Make predictions for batch of samples
 */
int predict_batch(XGBoostModel* model, FeatureVector* features_batch,
                  int batch_size, float* predictions) {
    if (!model || !model->is_loaded || !features_batch || !predictions || batch_size <= 0) {
        fprintf(stderr, "Invalid input parameters\n");
        return -1;
    }

    /* stack buffer - batch_size is bounded by prefetch.c's compact arrays
     * (max 516 candidates/fault), so this is at most ~8KB */
    float feature_matrix[batch_size * NUM_FEATURES];

    // Fill feature matrix and preprocess
    for (int i = 0; i < batch_size; i++) {
        FeatureVector temp_features = features_batch[i];
        preprocess_features(&temp_features);

        int base_idx = i * NUM_FEATURES;
        feature_matrix[base_idx + FEATURE_PC] = (float)temp_features.pc;
        feature_matrix[base_idx + FEATURE_OFFSET] = (float)temp_features.offset;
        feature_matrix[base_idx + FEATURE_DELTA] = temp_features.delta;
        feature_matrix[base_idx + FEATURE_OFFSET_FROM_FAULTING] = (float)temp_features.offset_from_faulting;
    }

    /* Inplace prediction directly from feature_matrix - no DMatrix
     * create/destroy at all (not even a reused one). __array_interface__
     * just describes the buffer we already have; NULL proxy is fine since
     * we have no extra meta info (categorical maps, base_margin, etc) to
     * attach - PC is a plain numeric feature. */
    char values_json[160];
    snprintf(values_json, sizeof(values_json),
        "{\"data\": [%llu, false], \"shape\": [%d, %d], \"typestr\": \"<f4\", \"version\": 3}",
        (unsigned long long)(uintptr_t)feature_matrix, batch_size, NUM_FEATURES);

    bst_ulong const *out_shape;
    bst_ulong out_dim;
    const float* out_result;

    if (XGBoosterPredictFromDense(model->booster, values_json, PREDICT_CONFIG_JSON,
                                   NULL, &out_shape, &out_dim, &out_result) != 0) {
        fprintf(stderr, "Failed to make batch predictions: %s\n", XGBGetLastError());
        return -1;
    }

    if (out_dim < 1 || out_shape[0] != (bst_ulong)batch_size) {
        fprintf(stderr, "Unexpected batch prediction output shape (dim=%lu, shape[0]=%lu, expected %d)\n",
                out_dim, out_dim >= 1 ? out_shape[0] : 0, batch_size);
        return -1;
    }

    // Copy results
    memcpy(predictions, out_result, batch_size * sizeof(float));

    return 0;
}

/**
 * Convert probability to binary prediction (threshold = 0.5)
 */
int probability_to_prediction(float probability) {
    return (probability >= 0.5) ? 1 : 0;
}

/**
 * Calculate prediction metrics
 */
void calculate_metrics(int* predictions, int* actual_labels, int count,
                      PredictionMetrics* metrics) {
    if (!predictions || !actual_labels || !metrics || count <= 0) {
        return;
    }

    metrics->cache_hit = 0;
    metrics->cache_miss = 0;
    metrics->misprefetch = 0;

    for (int i = 0; i < count; i++) {
        if (actual_labels[i] == 1 && predictions[i] == 1) {
            metrics->cache_hit++;
        } else if (actual_labels[i] == 1 && predictions[i] == 0) {
            metrics->cache_miss++;
        } else if (actual_labels[i] == 0 && predictions[i] == 1) {
            metrics->misprefetch++;
        }
    }

    // Calculate precision: cache_hits / (cache_hits + misprefetch)
    int total_predicted_positive = metrics->cache_hit + metrics->misprefetch;
    metrics->precision = (total_predicted_positive > 0) ?
                        (double)metrics->cache_hit / total_predicted_positive : 0.0;

    // Calculate recall: cache_hits / (cache_hits + cache_miss)
    int total_actual_positive = metrics->cache_hit + metrics->cache_miss;
    metrics->recall = (total_actual_positive > 0) ?
                     (double)metrics->cache_hit / total_actual_positive : 0.0;

    // Calculate accuracy
    int correct_predictions = 0;
    for (int i = 0; i < count; i++) {
        if (predictions[i] == actual_labels[i]) {
            correct_predictions++;
        }
    }
    metrics->accuracy = (double)correct_predictions / count;
}

/**
 * Free model resources
 */
void free_model(XGBoostModel* model) {
    if (model) {
        if (model->booster) {
            XGBoosterFree(model->booster);
        }
        free(model);
    }
}

#endif // NATIVE_ONLY

void init_prefetcher() {
#ifdef NATIVE_ONLY
    log_info("prefetch inference mode: native (hand-compiled trees, "
             "libxgboost not linked into this build)");
#else
    /* native mode never touches global_model (page_postfetch_preds routes
     * straight to native_predict_batch), so skip calling into libxgboost at
     * all here - XGBoosterLoadModel() internally runs an OpenMP parallel-for
     * to load tree nodes, which spins up libgomp's thread pool sized to
     * nproc; those threads then busy-spin at a barrier for the rest of the
     * process's life instead of blocking, permanently starving the real
     * mcf/handler threads of CPU on a fully-subscribed core count. Loading
     * the model was only ever meant as a load-succeeds sanity check, not
     * something the native path depends on. */
    if (native_mode_enabled()) {
        log_info("prefetch inference mode: native (hand-compiled trees, no "
                 "libxgboost) - skipping libxgboost model load entirely");
        return;
    }

    const char* model_path = getenv(XGBOOST_MODEL_PATH_ENV);
    if (!model_path)
        model_path = XGBOOST_MODEL_PATH_DEFAULT;

    pthread_mutex_lock(&model_init_lock);

    // Check if already initialized
    if (global_model != NULL && global_model->is_loaded) {
        pthread_mutex_unlock(&model_init_lock);
        return;
    }

    log_info("initializing prefetcher, model path: %s", model_path);

    global_model = init_model();
    if (!global_model) {
        log_err("prefetch model structure allocation failed");
        pthread_mutex_unlock(&model_init_lock);
        return;
    }

    if (load_model(global_model, model_path) != 0) {
        log_err("prefetch model loading failed");
        free_model(global_model);
        global_model = NULL;
        pthread_mutex_unlock(&model_init_lock);
        return;
    }
    log_info("prefetcher initialized successfully");

    pthread_mutex_unlock(&model_init_lock);
#endif // NATIVE_ONLY
}

/*
 * This function is run before the page is fetched.
 * It can do prefetching based on non-page content related
 * information.
 * Features: input features computed at pagefault.
 * Reponse arr: Output predictions.
 */
unsigned long page_prefetch_preds(FeatureVector features[], int *response_arr) {
    return 0;
}

/*
 * This function allows fetching pages after the page
 * contents are accessible. It fetches additional pages
 * after the currently faulted page is already fetched.
 * Features: input features computed at pagefault.
 * Reponse arr: Output predictions.
 */
unsigned long page_postfetch_preds(FeatureVector features[], int *response_arr, int batch_size) {
    /* native path: hand-compiled tree evaluator baked in at build time from
     * native_tree_model.h, no DMatrix/JSON/libxgboost call at all */
#ifdef NATIVE_ONLY
    return native_predict_batch(features, batch_size, response_arr);
#else
    if (native_mode_enabled())
        return native_predict_batch(features, batch_size, response_arr);

    float probs[batch_size];

    if (!global_model || !global_model->is_loaded) {
        log_warn_ratelimited("prefetch model not initialized, call init_prefetcher() first");
        return 1;
    }

    /* one DMatrix create/predict/free for the whole batch instead of
     * batch_size separate ones (each candidate used to cost its own
     * predict_single() call) - the DMatrix create/destroy overhead, not
     * the actual tree evaluation, dominates cost per call regardless of
     * row count, so batching amortizes nearly all of it away */
    if (predict_batch(global_model, features, batch_size, probs) != 0) {
        fprintf(stderr, "Batch prediction failed\n");
        memset(response_arr, 0, batch_size * sizeof(*response_arr));
        return 1;
    }

    for (int i = 0; i < batch_size; i++)
        response_arr[i] = probability_to_prediction(probs[i]);

    return 0;
#endif // NATIVE_ONLY
}

#endif // DO_PREFETCH
