/*
 * common.h - remote memory common for 
 * shenango and handler threads
 */

#ifndef __RMEM_COMMON_H__
#define __RMEM_COMMON_H__

#include <stddef.h>
#include "base/types.h"
#include "rmem/handler.h"

/* global remote memory settings */
extern bool rmem_enabled;
extern rmem_backend_t rmbackend_type;
extern uint64_t local_memory;
extern double eviction_threshold;
extern int evict_batch_size;
extern int evict_ngens;
extern int evict_nprio;
extern int fsampler_samples_per_sec;

/* global state */
extern int nhandlers;
extern hthread_t** handlers;
extern atomic64_t memory_used;
extern atomic64_t max_memory_used;
extern atomic64_t memory_allocd;
extern atomic64_t memory_freed;
extern bool rmem_inited;
/* incremented once per real fault serviced, DO_PREFETCH builds only (see
 * fault_done() in fault.c) - used to stamp/age prefetch-staged pages (see
 * PREFETCH_STAGING_MIN_FAULT_AGE in config.h and staging_add()/
 * drain_staging() in eviction.c). Declared unconditionally since eviction.c
 * (which is always compiled) reads it; only the increment site is
 * DO_PREFETCH-gated. */
extern atomic64_t prefetch_fault_counter;

#ifdef DO_IDEAL_PREFETCH_SIM
/* idealized-prefetcher simulation (opt-in: make fltrace.so
 * DO_IDEAL_PREFETCH_SIM=1) - see local_post_read() in rmem_local.c. Every
 * 8th real local-backend read pays ideal_prefetch_delay_us (plus a static
 * 4us page-copy cost, IDEAL_PREFETCH_READ_DELAY_US in rmem_local.c) of injected
 * busy-wait latency (simulating an un-prefetched page needing a real RDMA
 * round trip); the other 7 pay nothing (simulating pages a perfect
 * prefetcher already fetched for free). Set via FLTRACE_IDEAL_PREFETCH_DELAY_US
 * (microseconds, 0 = disabled) - a runtime knob rather than a compile-time
 * one so a delay sweep doesn't require rebuilding fltrace.so per point. */
extern uint64_t ideal_prefetch_delay_us;
extern atomic64_t ideal_prefetch_read_counter;
#endif

/* thread-local */
extern __thread pgthread_t current_kthread_id;
extern __thread bool __from_runtime;

/* track application vs runtime */
#define RUNTIME_ENTER()             \
  do {                              \
    __from_runtime = true;          \
  } while (0)
#define RUNTIME_EXIT()              \
  do {                              \
	assert(__from_runtime);	   		\
    __from_runtime = false;         \
  } while (0)
#define IN_RUNTIME()      (__from_runtime)
#define NOT_IN_RUNTIME()  (!__from_runtime)

/* init & destroy */
int rmem_common_init(unsigned long nslabs, int pin_handlers_start_core,
    int pin_handlers_end_core, int fsampler_samples_per_sec);
int rmem_common_init_thread(int* new_chan_id, uint64_t* stats_ptr, 
    pgthread_t kthr_id);
int rmem_common_destroy_thread(void);
int rmem_common_destroy(void);

#endif  // __RMEM_COMMON_H__