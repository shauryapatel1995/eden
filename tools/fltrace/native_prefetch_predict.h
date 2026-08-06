#ifndef __NATIVE_PREFETCH_PREDICT_H__
#define __NATIVE_PREFETCH_PREDICT_H__

#include "rmem/prefetch.h"

/* hand-compiled tree evaluators - separate models for spatial/next-N
 * (Type=0) vs pointer-chase (Type=1) candidates, see
 * native_predict_spatial.c / native_predict_pointer.c */
int native_predict_batch_spatial(FeatureVector *features_batch, int batch_size,
                          int *response_arr);
int native_predict_batch_pointer(FeatureVector *features_batch, int batch_size,
                          int *response_arr);

#endif // __NATIVE_PREFETCH_PREDICT_H__
