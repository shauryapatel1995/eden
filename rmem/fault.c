
/*
 * fault.c - fault handling common
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "base/list.h"
#include "rmem/backend.h"
#include "rmem/common.h"
#include "rmem/fault.h"
#include "rmem/page.h"
#include "rmem/pgnode.h"
#include "rmem/stats.h"
#include "rmem/uffd.h"
#include "rmem/prefetch.h"

/* fault handling common state */
__thread void* zero_page = NULL;
__thread char fstr[__FAULT_STR_LEN];

__thread unsigned int n_wait_q;
__thread struct list_head fault_wait_q;

#ifdef DO_PREFETCH
__thread FeatureVector features[600];
__thread int responses[600];
#endif

/*
 * Initialize the prefetcher and its per-thread feature buffers. Only does
 * anything when DO_PREFETCH is enabled - a predictor backend (e.g.
 * tools/fltrace/xgboost_prefetcher.c) must provide init_prefetcher().
 */
void prefetch_init(void)
{
#ifdef DO_PREFETCH
    int i;

    init_prefetcher();

    /* pointer candidates */
    for (i = 0; i < 512; i++) {
        features[i].pc = 0;
        features[i].offset = i;
        features[i].delta = -1;
        features[i].offset_from_faulting = 0;
    }

    /* next-N sequential candidates */
    for (i = 512; i < 600; i++) {
        features[i].pc = 0;
        features[i].offset = -1;
        features[i].delta = i;
        features[i].offset_from_faulting = -1;
    }
#endif
}

/**
 * Per-thread zero page support
 */

void zero_page_init_thread()
{
    size_t size;
    size = CHUNK_SIZE * RMEM_MAX_CHUNKS_PER_OP;
    zero_page = aligned_alloc(CHUNK_SIZE, size);
    assert(zero_page);
    memset(zero_page, 0, size);
}

void zero_page_free_thread()
{
    assert(zero_page);
    free(zero_page);
}

/**
 * Fault handling
 */

/* are we already in the state that the fault hoped to acheive? */
bool is_fault_serviced(fault_t* f, bool locked)
{    
    pgflags_t pflags;
    bool page_present, page_dirty, page_evicting;
    bool cannot_trust_present_flag;

    pflags = get_page_flags(f->mr, f->page);
    page_present = !!(pflags & PFLAG_PRESENT);
    page_dirty = !!(pflags & PFLAG_DIRTY);
    page_evicting = !!(pflags & PFLAG_EVICT_ONGOING);

    /* Without the page lock, PFLAG_PRESENT is reliable except for some 
     * window during eviction (after madvise() removed the page and before 
     * clearing the PRESENT bit when a kernel fault could get here with the 
     * PRESENT bit set while the page is absent; in this case, be on the 
     * safe side and return not serviced when the page is not locked */
    cannot_trust_present_flag = !locked && (f->from_kernel && page_evicting);

    if(page_present && !cannot_trust_present_flag){
        if (f->is_read)
            return true;
        if((f->is_write || f->is_wrprotect) && page_dirty)
            return true;
    }
    return false;
}

/* checks whether a page is prefetchable and, if so, locks it (caller is
 * responsible for clearing PFLAG_WORK_ONGOING once done with it) */
bool is_page_prefetchable(fault_t *f, unsigned long addr)
{
    struct region_t* mr;
    bool was_locked;
    pgflags_t oldflags, rflags;

    mr = f->mr;
    if (!is_in_memory_region_unsafe(mr, addr))
        return false;

    rflags = get_page_flags(mr, addr);
    /* this page hasn't been accessed by the app yet, don't prefetch it */
    if (!(rflags & PFLAG_REGISTERED))
        return false;

    /* already present
     * XXX: confirm if the other requirements from fault_can_rdahead()
     * are necessary here too */
    if (rflags & PFLAG_PRESENT)
        return false;

    if (rflags & PFLAG_WORK_ONGOING)
        return false;

    /* try locking */
    rflags = set_page_flags(mr, addr, PFLAG_WORK_ONGOING, &oldflags);
    was_locked = !!(oldflags & PFLAG_WORK_ONGOING);
    if (was_locked)
        return false;

    /* check present again after locking
     * XXX: do we need to run fault_can_rdahead() here too? */
    if (unlikely(rflags & PFLAG_PRESENT)) {
        /* this shouldn't happen unless there is an extreme race; someone
         * locked the page, changed its state and released it all in
         * between our earlier check and taking the lock */
        clear_page_flags(mr, addr, PFLAG_WORK_ONGOING, &oldflags);
        assert(!!(oldflags & PFLAG_WORK_ONGOING));
        return false;
    }
    return true;
}

/* checks if a page is in the same state as the faulting page to batch it
 * together as a part of rdahead. this function only checks page flags and
 * assumes that page locations relative to each other are already evaluated
 * for read-ahead */
bool fault_can_rdahead(pgflags_t rdahead_page, pgflags_t base_page)
{
    /* both pages must be present or not-present */
    if ((base_page & PFLAG_PRESENT) != (rdahead_page & PFLAG_PRESENT))
        return false;

    /* if present, both must be dirty or non-dirty */
    if (!!(base_page & PFLAG_PRESENT)) 
        if ((base_page & PFLAG_DIRTY) != (rdahead_page & PFLAG_DIRTY))
            return false;
    
    /* both pages must be registered or not-registered */
    if ((base_page & PFLAG_REGISTERED) != (rdahead_page & PFLAG_REGISTERED))
        return false;

    /* TODO: anything else? */
    return true;
}

/* get the eviction list farthest from the current evicting list (based on 
 * policy) to add the faulting page to */
int __always_inline get_highest_evict_gen(void)
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

/* after a prefetched page has been uffd-copied into the address space, we
 * must allocate a new page node to track it and add it to the eviction
 * lists (same as fault_alloc_page_nodes() below, but for a single
 * out-of-band prefetched page rather than the fault's own rdahead range) */
static inline void prefetch_alloc_page_nodes(unsigned long addr,
    struct region_t* mr, int prio)
{
    struct rmpage_node* pgnode;
    struct list_head new;
    struct page_list* evict_gen;
    pgidx_t pgidx;

    assert(prio >= 0 && prio < evict_nprio);

    list_head_init(&new);
    pgnode = rmpage_node_alloc();
    assert(pgnode);

    /* each page node gets an MR reference too which gets removed
     * when the page is evicted out */
    __get_mr(mr);
    pgnode->mr = mr;
    pgnode->addr = addr;
    pgnode->evict_prio = prio;
    list_add_tail(&new, &pgnode->link);

    pgidx = rmpage_get_node_id(pgnode);
    pgidx = set_page_index(pgnode->mr, pgnode->addr, pgidx);
    assertz(pgidx); /* old index must be 0 */

    /* XXX: prefetched pages don't currently participate in the
     * do-not-evict (DNE) list, unlike fault_alloc_page_nodes() below */
    evict_gen = &evict_gens[get_highest_evict_gen()];
    spin_lock(&evict_gen->lock);
    list_append_list(&evict_gen->pages[prio], &new);
    evict_gen->npages += 1;
    spin_unlock(&evict_gen->lock);
}

/* after the faulting page (and read-ahead) has been uffd-copied into the
 * address space, we must allocate new page nodes to track them and add the
 * nodes to the eviction lists */
static inline void fault_alloc_page_nodes(fault_t* f)
{
    int i, prio;
    struct rmpage_node* pgnode;
    struct list_head new;
    struct page_list* evict_gen;
    pgidx_t pgidx;

    /* prio level */
    prio = f->evict_prio;
    assert(prio >= 0 && prio < evict_nprio);

    /* newly fetched pages - alloc page nodes (for both the base page and 
     * the read-ahead) */
    list_head_init(&new);
    for (i = 0; i <= f->rdahead; i++) { 
        /* get a page node */
        pgnode = rmpage_node_alloc();
        assert(pgnode);

        /* each page node gets an MR reference too which gets removed 
         * when the page is evicted out */
        __get_mr(f->mr);
        pgnode->mr = f->mr;
        pgnode->addr = f->page + i * CHUNK_SIZE;
        pgnode->evict_prio = prio;
        list_add_tail(&new, &pgnode->link);

        pgidx = rmpage_get_node_id(pgnode);
        pgidx = set_page_index(pgnode->mr, pgnode->addr, pgidx);
        assertz(pgidx); /* old index must be 0 */
    }

#ifdef EVICTION_DNE_ON
    int popped;
    struct list_head popped;

    /* check for space in DNE list or make space otherwise */
    BUILD_ASSERT(RMEM_DNE_MAX_PAGES >= (1 + FAULT_MAX_RDAHEAD_SIZE));
    list_head_init(&popped);
    spin_lock(&dne_pages.locks[prio]);
    overhead = ((int) dne_pages.npages[prio] + (1 + f->rdahead)) - RMEM_DNE_MAX_PAGES;
    for (i = 0; i < overhead; i++) {
        pgnode = list_pop(&dne_pages.pages[prio], struct rmpage_node, link);
        assert(pgnode);
        dne_pages.npages[prio]--;
        list_add_tail(&popped, &pgnode->link);
    }

    /* add new pages to DNE list */
    list_append_list(&dne_pages.pages[prio], &new);
    dne_pages.npages[prio] += (1 + f->rdahead);
    assert(dne_pages.npages[prio] <= RMEM_DNE_MAX_PAGES);
    spin_unlock(&dne_pages.locks[prio]);

    /* add any DNE popped pages to highest evict list */
    if(overhead > 0) {
        assert(!list_empty(&popped));
        evict_gen = &evict_gens[get_highest_evict_gen()];
        spin_lock(&evict_gen->lock);
        list_append_list(&evict_gen->pages[prio], &popped);
        evict_gen->npages += overhead;
        spin_unlock(&evict_gen->lock);
    }
#else
    /* add new pages to highest evict list */
    evict_gen = &evict_gens[get_highest_evict_gen()];
    spin_lock(&evict_gen->lock);
    list_append_list(&evict_gen->pages[prio], &new);
    evict_gen->npages += (1 + f->rdahead);
    spin_unlock(&evict_gen->lock);
#endif
}

/* serve zero pages for first-time faults without going to the backend */
static inline void fault_serve_zero_pages(fault_t* f, int nchunks)
{
    int nretries, ret;
    bool nowake, wrprotect;
    pgflags_t flags;

    /* Two options for zero page: UFFD_ZERO or UFFD_COPY a zero page.
    * UFFD_ZERO currently maps a zero page where a write results in
    * a minor fault that never reaches the handler. UFFD_ZERO as 
    * a standalone operation is faster than UFFD_COPYing with zeros 
    * but when followed by dirtying (which is expected in most 
    * cases) UFFD_COPY is better as it avoids the minor fault. 
    * So we always treat zero page fault as a write fault */
    fault_upgrade_to_write(f, "zero page");

    /* map zero pages */
    nowake = !f->from_kernel;
    wrprotect = f->is_read;
    assert(nchunks == 1 + f->rdahead);
    ret = uffd_copy(userfault_fd, f->page, (unsigned long) zero_page, 
        nchunks * CHUNK_SIZE, wrprotect, nowake, true, &nretries);
    assertz(ret);
    RSTAT(UFFD_RETRIES) += nretries;
    
    /* set page flags */
    flags = PFLAG_REGISTERED | PFLAG_PRESENT | PFLAG_PRESENT_ZERO_PAGED;
    if (!wrprotect) flags |= PFLAG_DIRTY;
    ret = set_page_flags_range(f->mr, f->page, nchunks * CHUNK_SIZE, flags);
    assert(ret == nchunks);

    /* alloc page nodes */
    fault_alloc_page_nodes(f);
    
    /* done */
    log_debug("%s - added %d zero pages", FSTR(f), nchunks);
    RSTAT(FAULTS_ZP)++;
}

/* Called after a prefetched page is read from the backend. Takes mr/prio
 * as plain values rather than the fault that triggered the speculative
 * scan - that fault can (and, once prefetch reads are async, routinely
 * will) already be freed and recycled for an unrelated fault by the time
 * its prefetch candidates' reads actually complete, so nothing here may
 * dereference it */
int prefetch_read_done(unsigned long addr, void *bkend_buf,
    struct region_t *mr, int evict_prio)
{
    int n_retries, r;
    bool wrprotect, no_wake;
    size_t size;
    pgflags_t flags;

    assert(bkend_buf);
    wrprotect = true;
    no_wake = true;

    size = CHUNK_SIZE;
    r = uffd_copy(userfault_fd, addr, (unsigned long) bkend_buf, size,
        wrprotect, no_wake, true, &n_retries);
    assertz(r);
    RSTAT(UFFD_RETRIES) += n_retries;

    /* free the backend buffer - each prefetch candidate gets its own
     * (unlike a fault's f->bkend_buf, it's not shared/reused across
     * candidates), so it's ours to free here, same as fault_read_done()
     * does for f->bkend_buf */
    bkend_buf_free(bkend_buf);

    /* set page flags */
    flags = PFLAG_PRESENT;
    if (!wrprotect) flags |= PFLAG_DIRTY;
    set_page_flags_range(mr, addr, size, flags);

    /* add page node for the prefetched page */
    prefetch_alloc_page_nodes(addr, mr, evict_prio);

    return 0;
}

/* Called after reading the pages from the backend completed: uffd-copies the
 * page(s) into virtual memory and allocs page nodes */
int fault_read_done(fault_t* f)
{
    int n_retries, r;
    bool wrprotect, no_wake;
    size_t size;
    pgflags_t flags;

    /* uffd copy the page(s) back */
    assert(f->bkend_buf);
    wrprotect = f->is_read;
    no_wake = !f->from_kernel;
    size = (1 + f->rdahead) * CHUNK_SIZE;
    r = uffd_copy(userfault_fd, f->page, (unsigned long) f->bkend_buf, size,
        wrprotect, no_wake, true, &n_retries);
    assertz(r);
    RSTAT(UFFD_RETRIES) += n_retries;

    /* this uffd_copy() call is what actually wakes the faulting thread for
     * real (from_kernel) faults (no_wake is false exactly in that case,
     * above) - so the gap between when we first observed this fault and
     * right now is real wall-clock time the app thread spent blocked */
    if (f->from_kernel)
        RSTAT(APP_FAULT_WAIT_CYCLES) += rdtsc() - f->create_tsc;

    /* free the backend buffer */
    bkend_buf_free(f->bkend_buf);

    /* set page flags */
    flags = PFLAG_PRESENT;
    if (!wrprotect) flags |= PFLAG_DIRTY;
    set_page_flags_range(f->mr, f->page, size, flags);

    /* add page nodes for the pages */
    fault_alloc_page_nodes(f);

    return 0;
}

/* Called after servicing fault is completely done: removes lock on the page
 * and frees temporary resources */
void fault_done(fault_t* f, int chan_id, int *nevicts_needed)
{
    int i, r;
    pgthread_t owner_kthr;
    pgflags_t oldflags;
#ifdef DO_PREFETCH
    unsigned long postfetch_start_tsc;
#endif

#ifdef DO_PREFETCH
    /* speculatively prefetch pointer-chase candidates off the page we just
     * finished servicing, before releasing its lock. this runs on the
     * single handler thread, which can't poll for the next real uffd
     * fault while it's in here - see RSTAT_PREFETCH_SCAN_CYCLES */
    postfetch_start_tsc = rdtsc();
    page_postfetch(f, features, responses, chan_id, nevicts_needed);
    RSTAT(PREFETCH_SCAN_CYCLES) += rdtsc() - postfetch_start_tsc;
#endif

#ifdef DO_TRACING
    /* dump every nonzero pointer-sized candidate on the page while it's
     * still locked, for offline ground-truth labeling (was this candidate
     * actually chased next?) when building prefetcher training data.
     * present reflects whether the candidate's target page is already
     * resident (not a prefetch candidate at all) rather than a hardcoded
     * placeholder - values outside our tracked remote-memory region are
     * reported as not present since PFLAG_PRESENT is meaningless there */
    {
        uint64_t *p = (uint64_t*) f->page;
        unsigned long candidate_page;
        int present;
        for (i = 0; i < 512; i++, p++) {
            if (*p == 0)
                continue;
            candidate_page = *p & ~CHUNK_MASK;
            present = is_in_memory_region_unsafe(f->mr, candidate_page) &&
                !!(get_page_flags(f->mr, candidate_page) & PFLAG_PRESENT);
            fprintf(stderr, "Loc: %d, val: %lx, present:%d\n", i, *p, present);
        }
    }
#endif

    /* remove lock (in ascending order) */
    if (f->locked_pages) {
        for (i = 0; i <= f->rdahead; i++)
        {
            clear_page_flags_and_thread(f->mr, f->page + i * CHUNK_SIZE, 
                PFLAG_WORK_ONGOING, &oldflags, &owner_kthr);
            
            /* check that the page was locked and that the saved kthread thread id
            * (if this fault was originally from a kthread) is same or different 
            * the from current kthread id depending on if the fault was stolen */
            assert(!!(oldflags & PFLAG_WORK_ONGOING));
            if (owner_kthr) {
                assert(f->stolen_from_cq || owner_kthr == current_kthread_id);
                assert(!f->stolen_from_cq || owner_kthr != current_kthread_id);
            }
        }
    }

    /* see if the fault needs to explicitly wake up faulting thread in the 
     * kernel (because the page was already serviced by someone else) */
    if (f->uffd_explicit_wake) {
        assert(f->from_kernel || false);
        assert(f->rdahead == 0);
        r = uffd_wake(userfault_fd, f->page, CHUNK_SIZE);
        assertz(r);
    }

    RSTAT(FAULTS_DONE)++;
    log_debug("%s - fault done", FSTR(f));

    /* free */
    put_mr(f->mr);
    fault_free(f);
}

/* Gateway to common fault handling for shenango or handler cores after 
 * receiving a page fault */
enum fault_status handle_page_fault(int chan_id, fault_t* fault, 
    int* nevicts_needed, struct bkend_completion_cbs* cbs)
{
    struct region_t* mr;
    bool page_present, was_locked, no_wake, wrprotect;
    int i, ret, n_retries, nchunks, noverflow;
    pgflags_t pflags, rflags, oldflags;
    unsigned long addr;
    unsigned long long pressure;
    uint64_t start_tsc, duration;
    enum fault_status status;

    assert(nevicts_needed);
    *nevicts_needed = 0;

    /* see if this fault needs to be acted upon, because some other fault 
     * on the same page might have handled it by now */
    if (is_fault_serviced(fault, /*page locked=*/ false)) {
        /* some other fault (or a prefetch) already resolved the page by
         * the time we got to check - the app still waited from create_tsc
         * to now, so this needs the same accounting fault_read_done()
         * does for the non-redundant case */
        fault->uffd_explicit_wake = fault->from_kernel;
        log_debug("%s - fault done, was redundant", FSTR(fault));
        RSTAT(FAULTS_REDUNDANT)++;
        if (fault->from_kernel)
            RSTAT(APP_FAULT_WAIT_CYCLES) += rdtsc() - fault->create_tsc;
        return FAULT_DONE;
    }
    else {
        /* try getting a lock on the page */
        mr = fault->mr;
        assert(mr);

        pflags = set_page_flags(mr, fault->page, PFLAG_WORK_ONGOING, &oldflags);
        was_locked = !!(oldflags & PFLAG_WORK_ONGOING);
        if (unlikely(was_locked)) {
            /* someone else is working on it, check back later */
            log_debug("%s - saw ongoing work, going to wait", FSTR(fault));
            return FAULT_IN_PROGRESS;
        }
        else {
            /* locked; we are handling it */
            nchunks = 1;
            fault->locked_pages = true;
            log_debug("%s - no ongoing work, start handling", FSTR(fault));

            /* see if the fault got serviced during the locking */
            if (unlikely(is_fault_serviced(fault, /*locked=*/ true))) {
                /* same as the unlocked redundant check above */
                fault->uffd_explicit_wake = fault->from_kernel;
                log_debug("%s - fault done, was redundant", FSTR(fault));
                if (fault->from_kernel)
                    RSTAT(APP_FAULT_WAIT_CYCLES) += rdtsc() - fault->create_tsc;
                return FAULT_DONE;
            }

            /* at this point, we can check for read-ahead. see if we can get
             * a lock on the next few pages that have similar requirements
             * as the current page so we can make the same choices for them
             * throughout the fault handling */
#ifdef DO_RDAHEAD
            for (i = 1; i <= fault->rdahead_max; i++) {
                addr = fault->page + i * CHUNK_SIZE;
                if(!is_in_memory_region_unsafe(mr, addr))
                    break;

                /* see if the page has similar faulting requirements as the 
                 * the base page */
                rflags = get_page_flags(mr, addr);
                if (!fault_can_rdahead(rflags, pflags))
                    break;

                /* try locking */
                rflags = set_page_flags(mr, addr, PFLAG_WORK_ONGOING, &oldflags);
                was_locked = !!(oldflags & PFLAG_WORK_ONGOING);
                if (was_locked) 
                    break;

                /* check again after locking */
                if (unlikely(!fault_can_rdahead(rflags, pflags))) {
                    /* this shouldn't happen unless there is an extreme race;
                     * someone locked the page, changed its state and released
                     * it all in between our earlier check and taking a lock */
                    clear_page_flags(mr, addr, PFLAG_WORK_ONGOING, &oldflags);
                    assert(!!(oldflags & PFLAG_WORK_ONGOING));
                    break;
                }
                
                nchunks++;
                fault->rdahead++;
            }
#endif
            if (nchunks > 1) {
                RSTAT(RDAHEADS)++;
                RSTAT(RDAHEAD_PAGES) += fault->rdahead;
            }

            /* page present bit might have been updated just before we 
             * locked - we should check it again after taking the lock 
             * just in case! */
            page_present = !!(pflags & PFLAG_PRESENT);
            if (page_present) {
                wrprotect = (fault->is_wrprotect | fault->is_write);
                no_wake = fault->from_kernel ? false : true;

                /* we should have already handled the (page_present && 
                 * !wrprotect) case earlier in is_fault_serviced() */
                assert(wrprotect);
                
                /* write fault on existing page; just remove wrprotection */
                ret = uffd_wp_remove(userfault_fd, fault->page,
                    nchunks * CHUNK_SIZE, no_wake, true, &n_retries);
                assertz(ret);
                RSTAT(UFFD_RETRIES) += n_retries;

                /* this is what actually resolves the fault for real
                 * (from_kernel) faults on this path - e.g. one that got
                 * queued behind an in-flight prefetch and is only now
                 * resolving because that prefetch's completion already
                 * marked the page present above. fault_read_done() (the
                 * only other place APP_FAULT_WAIT_CYCLES is recorded)
                 * never runs for this path, so without this the real,
                 * nonzero wait such faults experience goes uncounted */
                if (fault->from_kernel)
                    RSTAT(APP_FAULT_WAIT_CYCLES) += rdtsc() - fault->create_tsc;

                /* done */
                log_debug("%s - removed wp for %d pages", FSTR(fault), nchunks);
                ret = set_page_flags_range(mr, fault->page,
                    nchunks * CHUNK_SIZE, PFLAG_DIRTY);
                assert(ret == nchunks);
                return FAULT_DONE;
            }
            
            /* page not present, upgrade wp to write */
            if (fault->is_wrprotect) {
                fault_upgrade_to_write(fault, "from wrprotect on no page");
                RSTAT(WP_UPGRADES)++;
            }

#ifndef TRACK_DIRTY
            /* no dirty page tracking means every fault is a write fault */
            if (fault->is_read)
                fault_upgrade_to_write(fault, "no TRACK_DIRTY");
#endif

            /* first time adding page, use zero page */
            if (!(pflags & PFLAG_REGISTERED)) {
#ifdef NO_ZERO_PAGE
                /* no zero page allowed for first serving; mark them 
                 * registered and proceed to read from remote; be careful with 
                 * this setting as it returns non-zero initial pages */
                ret = set_page_flags_range(mr, fault->page, 
                    nchunks * CHUNK_SIZE, PFLAG_REGISTERED);
                assert(ret == nchunks);
#else
                log_debug("%s - serving %d zero pages", FSTR(fault), nchunks);
                fault_serve_zero_pages(fault, nchunks);
                status = FAULT_DONE;
                goto pages_added_out;
#endif
            }
            
            /* once the read is posted, we would have already lost control of 
             * the fault when post_read returns as stealing is possible. 
             * Set any last fault params or update other information that we 
             * want other threads to see before we post - treat this 
             * akin to __store_release(fault) */
            if (current_kthread_id) {
                /* if this is a shenango kthread, save tid in page metadata */
                ret = set_page_thread_range(mr, fault->page, 
                    nchunks * CHUNK_SIZE, current_kthread_id);
                assert(ret == nchunks);
            }
            store_release(&fault->posted_chan_id, chan_id);

            /* send off page read */
            start_tsc = 0;
            do {
                ret = rmbackend->post_read(chan_id, fault);
                if (ret == EAGAIN) {
                    /* start the timer the first time we start retrying */
                    if (!start_tsc)
                        start_tsc = rdtsc();

                    /* read queue is full, nothing to do but repeat and keep 
                     * checking for completions to free request slots. We can 
                     * just check for one completion here? */
                    rmbackend->check_for_completions(chan_id, cbs, 
                        RMEM_MAX_COMP_PER_OP, NULL, NULL);
		            cpu_relax();
                }
            } while(ret == EAGAIN);
            assertz(ret);

            /* save wait time if any */
            if (start_tsc) {
                duration = rdtscp(NULL) - start_tsc;
                RSTAT(BACKEND_WAIT_CYCLES) += duration;
            }

            status = FAULT_READ_POSTED;
            goto pages_added_out;
        }
    }

pages_added_out:
    /* book some memory for the pages */
    assert(nchunks > 0);
    pressure = atomic64_add_and_fetch(&memory_used, nchunks * CHUNK_SIZE);
    log_debug("%s - memory pressure %llu, limit %lu", FSTR(fault), 
        pressure, local_memory);
    if (pressure > local_memory) {
        noverflow = (pressure - local_memory) / CHUNK_SIZE;
        *nevicts_needed = (noverflow < nchunks) ? noverflow : nchunks;
    }

    /* update maximum memory usage counter. FIXME: should use CAS! */
    if (pressure > atomic64_read(&max_memory_used))
        atomic64_write(&max_memory_used, pressure);

    log_debug("%s - %d page(s) added with return status %d, pressure %llu"
        " evicts %d", FSTR(fault), nchunks, status, pressure, *nevicts_needed);
    return status;
}