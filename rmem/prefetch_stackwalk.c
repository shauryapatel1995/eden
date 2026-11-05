/*
 * prefetch_stackwalk.c - Redis quicklist depth-walk prefetcher from the stack
 * (opt-in: make fltrace.so DO_PREFETCH_STACKWALK=1, optionally together with
 * DO_PREFETCH=1 ... for the spatial tree).
 *
 * Hand-written, Redis-specialized pointer prefetcher for LRANGE over a
 * quicklist (Redis 6.2: a doubly linked list of 32-byte quicklistNode
 * {prev, next, zl, sz:32 | count:16 ...}, each node owning a ziplist of
 * ~8KB at zl). Needs the 5.16.0-uffdsp+ kernel, which reports the faulting
 * thread's user SP in the uffd message (fault->sp, see handler.c).
 *
 * Runs right after the fault's own read is posted (same point as the spatial
 * prefetcher, while the app thread is still blocked), so its reads overlap
 * the fault's round trip:
 *   1. anchor: for each learned stack offset (words above SP), take the word
 *      as a node pointer and follow `next` up to STACKWALK_MAX_HOPS times
 *      until reaching the node C whose ziplist [zl, zl+sz) holds the faulting
 *      page. This check rejects stale/unrelated stack words, and also covers
 *      PCs where the stack still holds the previous node (quicklistNext has
 *      already advanced iter->current, which lives on the heap, but not yet
 *      entry->node).
 *   2. prefetch the rest of C's ziplist, then walk `next` through resident
 *      nodes up to depth D, prefetching every page of each node's ziplist.
 *      A node page that isn't resident is prefetched itself and ends the
 *      walk (we can't read it without blocking); next == NULL ends it too.
 * Pages already present or in flight are skipped by is_page_prefetchable(),
 * so consecutive faults only add the new tail of the window.
 *
 * Node reads happen on the handler thread: a node's page(s) must be present
 * and are locked (PFLAG_WORK_ONGOING, which eviction respects) while we copy
 * its 24 bytes + sz, since touching a non-present uffd page from the handler
 * would fault on ourselves and hang.
 *
 * Environment:
 *   FLTRACE_STACKWALK_RULES     the per-PC rule table ("tree"): semicolon list
 *                               of PC=soff[,soff...]; PC is absolute hex or
 *                               <library basename>+0x<offset>, resolved
 *                               against /proc/self/maps at startup. A fault
 *                               whose PC has no rule isn't walked. Default
 *                               STACKWALK_DEFAULT_RULES (from discovery on
 *                               DiLOS-exact Redis 6.2.4 LRANGE)
 *   FLTRACE_STACKWALK_DEPTH     nodes to walk past the current one (default 4)
 *   FLTRACE_STACKWALK_EXTENT    1 = use the node's sz: anchor on any page of
 *                               the ziplist and prefetch every page of each
 *                               ziplist (the first sw_d4 run, 2026-11-04).
 *                               Default 0 = head pages only: no struct
 *                               integers are interpreted; the anchor is the
 *                               node whose zl points into the faulting page
 *                               and only each node's zl page is prefetched
 *                               (later pages are left to other prefetchers)
 *   FLTRACE_STACKWALK_DISCOVER  1 = don't prefetch; instead scan the first
 *                               STACKWALK_DISCOVER_WORDS stack words at every
 *                               fault and, at exit, print to stderr which
 *                               (pc, offset, hops) anchored the faulting page
 *                               ("SWDISC" lines) - used to pick the offsets
 */

#ifdef DO_PREFETCH_STACKWALK

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rmem/backend.h"
#include "rmem/common.h"
#include "rmem/fault.h"
#include "rmem/page.h"
#include "rmem/prefetch.h"
#include "rmem/region.h"
#include "rmem/stats.h"

#define STACKWALK_MAX_SOFFS         4       /* per rule */
#define STACKWALK_MAX_RULES         16
#define STACKWALK_MAX_HOPS          2       /* stack node -> current node */
#define STACKWALK_MAX_DEPTH         32
#define STACKWALK_MAX_ZL_BYTES      (64 * 1024)
#define STACKWALK_MAX_CANDS         256
#define STACKWALK_MAX_STACK_WORDS   1024    /* never read beyond SP + 8KB */
#define STACKWALK_DISCOVER_WORDS    512
/* discovery (stackwalk/discover, 2026-11-04), LRANGE PCs only - the LPUSH
 * ones (__ziplistInsert soff 18, memmove+844 soff 123) are left out on
 * purpose: walking ahead of an insert prefetches old nodes for nothing.
 *   ziplistIndex (ziplist head page)               soff 22, 15
 *   __memmove_evex_unaligned_erms +390/+583 (2nd)  soff 13, 123, 15 */
#define STACKWALK_DEFAULT_RULES_EXTENT \
    "redis-server+0x53a79=22,15;" \
    "libc.so.6+0x1af006=13,123,15;" \
    "libc.so.6+0x1af0c7=13,123,15"
/* head pages only: just the ziplistIndex (head-page) faults; the memmove
 * faults are on page 2, which no pointer points into */
#define STACKWALK_DEFAULT_RULES_HEAD \
    "redis-server+0x53a79=22,15"

struct sw_rule { uint64_t pc; int nsoffs; int soffs[STACKWALK_MAX_SOFFS]; };
static int sw_inited;
static int sw_discover;
static int sw_depth = 4;
static int sw_extent;
static int sw_nrules;
static struct sw_rule sw_rules[STACKWALK_MAX_RULES];

struct sw_node {
    uint64_t addr;
    uint64_t next;
    uint64_t zl;
    uint32_t sz;
};

/* ---- discovery histogram: (pc, soff, hops) -> count ---- */
#define SWD_SLOTS   4096
struct swd_entry { uint64_t pc; int soff, hops; unsigned long n; };
static struct swd_entry swd[SWD_SLOTS];
static unsigned long swd_faults, swd_anchored, swd_dropped;

static void swd_add(uint64_t pc, int soff, int hops)
{
    unsigned long h = (pc * 0x9E3779B97F4A7C15UL) ^ (soff * 131 + hops);
    for (int i = 0; i < SWD_SLOTS; i++) {
        struct swd_entry *e = &swd[(h + i) % SWD_SLOTS];
        if (e->n && e->pc == pc && e->soff == soff && e->hops == hops) {
            e->n++;
            return;
        }
        if (!e->n) {
            e->pc = pc; e->soff = soff; e->hops = hops; e->n = 1;
            return;
        }
    }
    swd_dropped++;
}

static void swd_dump(void)
{
    if (!sw_discover)
        return;
    fprintf(stderr, "SWDISC faults %lu anchored %lu dropped %lu\n",
        swd_faults, swd_anchored, swd_dropped);
    for (int i = 0; i < SWD_SLOTS; i++)
        if (swd[i].n)
            fprintf(stderr, "SWDISC pc %lx soff %d hops %d n %lu\n",
                swd[i].pc, swd[i].soff, swd[i].hops, swd[i].n);
}

/* load base (lowest mapping) of the library/binary with this basename */
static uint64_t sw_lib_base(const char *name)
{
    char line[512], *path, *base;
    unsigned long lo, best = 0;
    FILE *maps = fopen("/proc/self/maps", "r");

    while (maps && fgets(line, sizeof(line), maps)) {
        line[strcspn(line, "\n")] = 0;
        if (!(path = strchr(line, '/')) || sscanf(line, "%lx-", &lo) != 1)
            continue;
        base = strrchr(path, '/') + 1;
        if (!strcmp(base, name) && (!best || lo < best))
            best = lo;
    }
    if (maps)
        fclose(maps);
    return best;
}

static void sw_init(void)
{
    const char *s;
    char buf[1024], *rule, *save, *eq, *plus, *tok, *save2;

    sw_inited = 1;
    if ((s = getenv("FLTRACE_STACKWALK_DEPTH")))
        sw_depth = atoi(s);
    if (sw_depth < 0)
        sw_depth = 0;
    if (sw_depth > STACKWALK_MAX_DEPTH)
        sw_depth = STACKWALK_MAX_DEPTH;
    sw_discover = (s = getenv("FLTRACE_STACKWALK_DISCOVER")) && atoi(s);
    sw_extent = (s = getenv("FLTRACE_STACKWALK_EXTENT")) && atoi(s);

    s = getenv("FLTRACE_STACKWALK_RULES");
    snprintf(buf, sizeof(buf), "%s", s ? s :
        (sw_extent ? STACKWALK_DEFAULT_RULES_EXTENT : STACKWALK_DEFAULT_RULES_HEAD));
    for (rule = strtok_r(buf, ";", &save); rule && sw_nrules < STACKWALK_MAX_RULES;
            rule = strtok_r(NULL, ";", &save)) {
        struct sw_rule *r = &sw_rules[sw_nrules];
        if (!(eq = strchr(rule, '=')))
            continue;
        *eq = 0;
        if ((plus = strchr(rule, '+'))) {
            *plus = 0;
            uint64_t base = sw_lib_base(rule);
            if (!base) {
                log_warn("stackwalk: %s not mapped, rule skipped", rule);
                continue;
            }
            r->pc = base + strtoul(plus + 1, NULL, 16);
        } else {
            r->pc = strtoul(rule, NULL, 16);
        }
        r->nsoffs = 0;
        for (tok = strtok_r(eq + 1, ",", &save2); tok && r->nsoffs < STACKWALK_MAX_SOFFS;
                tok = strtok_r(NULL, ",", &save2)) {
            int v = atoi(tok);
            if (v >= 0 && v < STACKWALK_MAX_STACK_WORDS)
                r->soffs[r->nsoffs++] = v;
        }
        log_info("stackwalk rule: pc %lx -> %d stack offset(s), first %d",
            r->pc, r->nsoffs, r->nsoffs ? r->soffs[0] : -1);
        sw_nrules++;
    }
    if (sw_discover)
        atexit(swd_dump);
    log_info("stackwalk prefetcher: depth %d, %s, %d rules%s", sw_depth,
        sw_extent ? "whole ziplists (sz)" : "head pages only", sw_nrules, sw_discover ? ", DISCOVERY mode (no prefetching)" : "");
}

static inline const struct sw_rule *sw_rule_for(uint64_t pc)
{
    for (int i = 0; i < sw_nrules; i++)
        if (sw_rules[i].pc == pc)
            return &sw_rules[i];
    return NULL;
}

/* lock a resident page for reading; false if absent or someone holds it */
static bool sw_lock_present(struct region_t *mr, unsigned long page)
{
    pgflags_t flags, oldflags;

    flags = get_page_flags(mr, page);
    if (!(flags & PFLAG_PRESENT) || (flags & PFLAG_WORK_ONGOING))
        return false;
    flags = set_page_flags(mr, page, PFLAG_WORK_ONGOING, &oldflags);
    if (oldflags & PFLAG_WORK_ONGOING)
        return false;
    if (!(flags & PFLAG_PRESENT)) {
        clear_page_flags(mr, page, PFLAG_WORK_ONGOING, &oldflags);
        return false;
    }
    return true;
}

static void sw_unlock(struct region_t *mr, unsigned long page)
{
    pgflags_t oldflags;
    clear_page_flags(mr, page, PFLAG_WORK_ONGOING, &oldflags);
}

/* status of reading a node */
enum { SW_OK, SW_BAD, SW_REMOTE };

/* read the node at addr (in f's region) if its page(s) are resident;
 * SW_REMOTE if a page isn't present (or is locked, e.g. in flight), SW_BAD
 * if addr can't be a node */
static int sw_read_node(fault_t *f, uint64_t addr, struct sw_node *n)
{
    struct region_t *mr = f->mr;
    unsigned long p0, p1;
    uint64_t *w;

    if (!addr || (addr & 7) || !is_in_memory_region_unsafe(mr, addr) ||
            !is_in_memory_region_unsafe(mr, addr + 31))
        return SW_BAD;
    p0 = addr & ~CHUNK_MASK;
    p1 = (addr + 31) & ~CHUNK_MASK;
    if (!sw_lock_present(mr, p0))
        return SW_REMOTE;
    if (p1 != p0 && !sw_lock_present(mr, p1)) {
        sw_unlock(mr, p0);
        return SW_REMOTE;
    }
    w = (uint64_t *) addr;
    n->addr = addr;
    n->next = w[1];
    n->zl = w[2];
    n->sz = (uint32_t) w[3];
    if (p1 != p0)
        sw_unlock(mr, p1);
    sw_unlock(mr, p0);
    RSTAT(STACKWALK_NODES_READ)++;
    if (!n->zl || !is_in_memory_region_unsafe(mr, n->zl) ||
            (sw_extent && (!n->sz || n->sz > STACKWALK_MAX_ZL_BYTES)))
        return SW_BAD;
    return SW_OK;
}

static inline bool sw_zl_holds(const struct sw_node *n, unsigned long page)
{
    if (!sw_extent)
        return (n->zl & ~CHUNK_MASK) == page;
    return (n->zl & ~CHUNK_MASK) <= page &&
        page <= ((n->zl + n->sz - 1) & ~CHUNK_MASK);
}

/* from a stack word, follow next up to STACKWALK_MAX_HOPS times to the node
 * whose ziplist holds the faulting page; returns hops taken or -1 */
static int sw_find_current(fault_t *f, uint64_t word, struct sw_node *cur)
{
    uint64_t addr = word;
    for (int h = 0; h <= STACKWALK_MAX_HOPS; h++) {
        if (sw_read_node(f, addr, cur) != SW_OK)
            return -1;
        if (sw_zl_holds(cur, f->page))
            return h;
        addr = cur->next;
    }
    return -1;
}

static inline bool sw_stack_ok(uint64_t sp, int nwords)
{
    /* the stack itself must not be uffd-managed memory (reading a
     * non-present page there would hang the handler) */
    return !get_region_by_addr_unsafe(sp & ~CHUNK_MASK) &&
        !get_region_by_addr_unsafe((sp + nwords * 8 - 1) & ~CHUNK_MASK);
}

static void sw_discover_fault(fault_t *f)
{
    uint64_t *stack = (uint64_t *) f->sp;
    struct sw_node cur;
    bool any = false;
    int h;

    swd_faults++;
    if (!sw_stack_ok(f->sp, STACKWALK_DISCOVER_WORDS))
        return;
    for (int soff = 0; soff < STACKWALK_DISCOVER_WORDS; soff++) {
        uint64_t w = stack[soff];
        if (!is_in_memory_region_unsafe(f->mr, w))
            continue;
        if ((h = sw_find_current(f, w, &cur)) >= 0) {
            swd_add(f->pc, soff, h);
            any = true;
        }
    }
    if (any)
        swd_anchored++;
}

/* queue a page for prefetching if prefetchable (locks it) */
static inline void sw_add(fault_t *f, uint64_t *cands, int *n, unsigned long page)
{
    if (*n < STACKWALK_MAX_CANDS && is_page_prefetchable(f, page))
        cands[(*n)++] = page;
}

static void sw_add_zl(fault_t *f, uint64_t *cands, int *n,
    const struct sw_node *node, unsigned long from_page)
{
    unsigned long last = (node->zl + node->sz - 1) & ~CHUNK_MASK;
    for (unsigned long p = from_page; p <= last; p += CHUNK_SIZE)
        sw_add(f, cands, n, p);
}

void page_prefetch_stackwalk(fault_t *f, int chan_id)
{
    uint64_t cands[STACKWALK_MAX_CANDS];
    uint64_t *stack, addr;
    struct sw_node cur, node;
    void *bkend_buf;
    pgflags_t oldflags;
    int ncands = 0, nprefetches = 0, depth, i, k, st;
    unsigned long start_tsc;
    bool found = false;
    const struct sw_rule *rule;

    if (unlikely(!sw_inited))
        sw_init();
    if (!f->sp)
        return;
    start_tsc = rdtsc();
    RSTAT(STACKWALK_CALLS)++;

    if (sw_discover) {
        sw_discover_fault(f);
        goto out;
    }
    /* the per-PC rule: which stack words hold the current node here */
    if (!(rule = sw_rule_for(f->pc)) ||
            !sw_stack_ok(f->sp, STACKWALK_MAX_STACK_WORDS))
        goto out;

    /* 1. anchor */
    stack = (uint64_t *) f->sp;
    for (i = 0; i < rule->nsoffs && !found; i++)
        found = sw_find_current(f, stack[rule->soffs[i]], &cur) >= 0;
    if (!found)
        goto out;
    RSTAT(STACKWALK_ANCHORED)++;

    /* 2. rest of the current ziplist, then walk */
    if (sw_extent)
        sw_add_zl(f, cands, &ncands, &cur, f->page + CHUNK_SIZE);
    addr = cur.next;
    for (depth = 0; depth < sw_depth; depth++) {
        if (!addr) {
            RSTAT(STACKWALK_STOP_END)++;
            break;
        }
        st = sw_read_node(f, addr, &node);
        if (st == SW_REMOTE) {
            sw_add(f, cands, &ncands, addr & ~CHUNK_MASK);
            RSTAT(STACKWALK_STOP_REMOTE)++;
            break;
        }
        if (st == SW_BAD)
            break;
        if (sw_extent)
            sw_add_zl(f, cands, &ncands, &node, node.zl & ~CHUNK_MASK);
        else
            sw_add(f, cands, &ncands, node.zl & ~CHUNK_MASK);
        addr = node.next;
    }
    RSTAT(STACKWALK_DEPTH_SUM) += depth;

    /* 3. post, nearest first; best-effort like prefetch_act() */
    for (k = 0; k < ncands; k++) {
        bkend_buf = bkend_buf_alloc();
        if (!bkend_buf) {
            clear_page_flags(f->mr, cands[k], PFLAG_WORK_ONGOING, &oldflags);
            continue;
        }
        set_page_flags(f->mr, cands[k], PFLAG_PREFETCHED, &oldflags);
        if (rmbackend->post_read_prefetch(chan_id, f, cands[k], bkend_buf)) {
            bkend_buf_free(bkend_buf);
            RSTAT(PREFETCH_BACKEND_BUSY)++;
            for (; k < ncands; k++)
                clear_page_flags(f->mr, cands[k],
                    PFLAG_WORK_ONGOING | PFLAG_PREFETCHED, &oldflags);
            break;
        }
        nprefetches++;
    }
    RSTAT(STACKWALK_PREFETCHES) += nprefetches;
    prefetch_book_pressure(f, nprefetches);

out:
    RSTAT(STACKWALK_CYCLES) += rdtsc() - start_tsc;
}

#endif /* DO_PREFETCH_STACKWALK */
