/*
 * eviction.h - eviction helpers
 */

#ifndef __EVICTION_H__
#define __EVICTION_H__

#include "base/sampler.h"
#include "rmem/backend.h"
#include "rmem/region.h"

/**
 * Eviction main
 */
int do_eviction(int chan_id, struct bkend_completion_cbs* cbs, int max_batch_size);
int owner_write_back_completed(struct region_t* mr, unsigned long addr, size_t size);
int stealer_write_back_completed(struct region_t* mr, unsigned long addr, size_t size);

/**
 * Page LRU lists support
 */

struct page_list {
    struct list_head pages[EVICTION_MAX_PRIO];
    size_t npages;
    spinlock_t lock;
};
struct page_list_per_prio {
    struct list_head pages[EVICTION_MAX_PRIO];
    size_t npages[EVICTION_MAX_PRIO];
    spinlock_t locks[EVICTION_MAX_PRIO];
};
extern struct page_list evict_gens[EVICTION_MAX_GENS];
extern struct page_list_per_prio dne_pages;
extern int evict_gen_mask;
extern int evict_gen_now;
extern unsigned long evict_epoch_now;
extern struct sampler epoch_sampler;

/* prefetch staging area - see PREFETCH_STAGING_MAX_PAGES in config.h. Only
 * pages[0] is used (staging doesn't distinguish eviction priority levels).
 * Always compiled (like evict_gens itself) rather than gated on DO_PREFETCH:
 * its only caller (prefetch_alloc_page_nodes() in fault.c) lives in shared,
 * unconditionally-compiled code, so gating this on DO_PREFETCH left an
 * unresolved symbol in non-DO_PREFETCH builds - harmless at build time for a
 * shared library, but a runtime crash waiting to happen if that path were
 * ever actually reached. */
extern struct page_list staging_pages;
struct rmpage_node;    /* forward decl - full definition in pgnode.h */
void staging_add(struct rmpage_node* pgnode, int prio);

/* get the eviction list farthest from the current evicting list (based on
 * policy) to add a new page to. Was previously __always_inline defined
 * directly in fault.c (its only caller at the time) - moved here as a
 * regular static inline header function once eviction.c/staging_add()
 * needed to call it too, since __always_inline functions require their
 * body visible at every call site and can't be declared-then-linked
 * cross-TU like a normal function. */
static inline int get_highest_evict_gen(void)
{
#ifdef SC_EVICTION
    assert(evict_ngens == 2 && evict_gen_mask == 1);
    return (ACCESS_ONCE(evict_gen_now) + 1) & 1;
#endif
#ifdef LRU_EVICTION
    return (ACCESS_ONCE(evict_gen_now) + evict_ngens - 1) & evict_gen_mask;
#endif
    return 0;
}

int eviction_init(void);
int eviction_init_thread(void);
void eviction_exit(void);

#endif  // __EVICTION_H__