#ifndef __NATIVE_PREFETCH_PREDICT_H__
#define __NATIVE_PREFETCH_PREDICT_H__

#include "rmem/prefetch.h"

/* hand-compiled tree evaluator - see native_prefetch_predict.c */
int native_predict_batch(FeatureVector *features_batch, int batch_size,
                          int *response_arr);

#endif // __NATIVE_PREFETCH_PREDICT_H__
