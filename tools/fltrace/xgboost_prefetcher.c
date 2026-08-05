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
#include <stdatomic.h>
#include <pthread.h>
#include <dlfcn.h>
#include "rmem/common.h"
#include "rmem/prefetch.h"

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

__attribute__((constructor))
void xgboost_pre_init(void) {
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

/**
 * Make prediction for a single sample
 */
int predict_single(XGBoostModel* model, FeatureVector* features, float* prediction) {
    if (!model || !model->is_loaded || !features || !prediction) {
        fprintf(stderr, "Invalid input parameters\n");
        return -1;
    }

    // Preprocess features
    preprocess_features(features);

    // Prepare feature array
    float feature_array[NUM_FEATURES];
    feature_array[FEATURE_PC] = (float)features->pc;
    feature_array[FEATURE_OFFSET] = (float)features->offset;
    feature_array[FEATURE_DELTA] = features->delta;
    feature_array[FEATURE_OFFSET_FROM_FAULTING] = (float)features->offset_from_faulting;

    // Create DMatrix for single prediction
    DMatrixHandle dtest;
    if (XGDMatrixCreateFromMat(feature_array, 1, NUM_FEATURES, -1, &dtest) != 0) {
        fprintf(stderr, "Failed to create DMatrix\n");
        return -1;
    }

    // Make prediction
    bst_ulong out_len;
    const float* out_result;

    if (XGBoosterPredict(model->booster, dtest, 0, 0, 0, &out_len, &out_result) != 0) {
        fprintf(stderr, "Failed to make prediction\n");
        XGDMatrixFree(dtest);
        return -1;
    }

    if (out_len != 1) {
        fprintf(stderr, "Unexpected prediction output length: %lu\n", out_len);
        XGDMatrixFree(dtest);
        return -1;
    }

    *prediction = out_result[0];

    /* TEMP DEBUG: check raw feature values reaching the model and the raw
     * probability it outputs, to see whether pc is sane and whether scores
     * ever approach the 0.5 threshold */
    {
        static _Atomic int __calls = 0;
        static _Atomic float __max_prob = 0;
        int n = atomic_fetch_add(&__calls, 1);
        float prev_max = atomic_load(&__max_prob);
        while (*prediction > prev_max &&
               !atomic_compare_exchange_weak(&__max_prob, &prev_max, *prediction))
            ;
        if (n < 20) {
            log_info("TEMP DEBUG: predict#%d pc=%.0f offset=%.0f delta=%.6f "
                "offset_from_faulting=%.0f prob=%.6f",
                n, feature_array[FEATURE_PC], feature_array[FEATURE_OFFSET],
                feature_array[FEATURE_DELTA],
                feature_array[FEATURE_OFFSET_FROM_FAULTING], *prediction);
        }
        if (n > 0 && n % 100000 == 0) {
            log_info("TEMP DEBUG: %d predictions so far, max prob seen: %.6f",
                n, atomic_load(&__max_prob));
        }
    }

    XGDMatrixFree(dtest);
    return 0;
}

/**
 * Make predictions for batch of samples
 */
int predict_batch(XGBoostModel* model, FeatureVector* features_batch,
                  int batch_size, float* predictions) {
    if (!model || !model->is_loaded || !features_batch || !predictions || batch_size <= 0) {
        fprintf(stderr, "Invalid input parameters\n");
        return -1;
    }

    // Prepare feature matrix
    float* feature_matrix = (float*)malloc(batch_size * NUM_FEATURES * sizeof(float));
    if (!feature_matrix) {
        fprintf(stderr, "Failed to allocate memory for feature matrix\n");
        return -1;
    }

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

    // Create DMatrix
    DMatrixHandle dtest;
    if (XGDMatrixCreateFromMat(feature_matrix, batch_size, NUM_FEATURES, -1, &dtest) != 0) {
        fprintf(stderr, "Failed to create DMatrix for batch\n");
        free(feature_matrix);
        return -1;
    }

    // Make predictions
    bst_ulong out_len;
    const float* out_result;

    if (XGBoosterPredict(model->booster, dtest, 0, 0, 0, &out_len, &out_result) != 0) {
        fprintf(stderr, "Failed to make batch predictions\n");
        XGDMatrixFree(dtest);
        free(feature_matrix);
        return -1;
    }

    if (out_len != (bst_ulong)batch_size) {
        fprintf(stderr, "Unexpected batch prediction output length: %lu (expected %d)\n",
                out_len, batch_size);
        XGDMatrixFree(dtest);
        free(feature_matrix);
        return -1;
    }

    // Copy results
    memcpy(predictions, out_result, batch_size * sizeof(float));

    XGDMatrixFree(dtest);
    free(feature_matrix);
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

void init_prefetcher() {
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

    if (!global_model || !global_model->is_loaded) {
        log_warn_ratelimited("prefetch model not initialized, call init_prefetcher() first");
        return 1;
    }

    for(int i = 0; i < batch_size; i++) {
        float prediction_prob;
        response_arr[i] = 0;
        assert(global_model);
        assert(features != NULL);
        if (predict_single(global_model, &features[i], &prediction_prob) == 0) {
            response_arr[i] = probability_to_prediction(prediction_prob);
        } else {
            fprintf(stderr, "Single prediction failed\n");
        }
    }

    return 0;
}

#endif // DO_PREFETCH
