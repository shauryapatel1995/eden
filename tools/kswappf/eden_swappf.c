// SPDX-License-Identifier: GPL-2.0
/*
 * eden_swappf - eden's PySTreeD trees as a Linux swap-in prefetcher (kernel
 * 6.6 "swappf": include/linux/swap_prefetch.h), plus a stack-anchor tracer.
 *
 * Spatial stage: on each swap-in fault of the target process, evaluates that
 * fault PC's spatial tree on pages +1..+next_n and asks the kernel to read
 * the predicted ones together with the faulting page. Features match eden's
 * (rmem/prefetch.c page_prefetch_spatial()):
 *   PC         - the user-space PC of the fault
 *   prev_pc    - the PC of this process's previous swap-in fault
 *   prev_delta - page delta between its two previous swap-in faults
 *   offset = -1, offset_from_faulting = -1 - (8-byte slot of the fault in
 *   its page), delta = k for page +k
 *
 * Stack stage (stack=1): eden's rmem/prefetch_stack.c in the kernel. For a
 * PC with a stack tree, the words at its anchor slots above the faulting
 * thread's SP (from the trace, st_stack_anchor_slots()) are read; such a
 * word is an anchor if it points at resident memory with a slot within
 * +-64 bytes (same page) pointing into the faulting page. The slots within
 * +-64 bytes of the anchor target are depth-0 candidates, and for anchor
 * slots some depth-1 box admits (Parent_slot), the slots within +-64 bytes
 * of the object they point to (if resident) are depth-1 candidates:
 *   Offset = the slot's index in its page, Offset_from_faulting = slot -
 *   the address its parent points to (in slots), Delta = candidate page -
 *   faulting page, Depth, Parent_slot = the anchor slot followed (depth 1).
 *   Each candidate's page C also gives C+1..C+STK_EXTENT (same features,
 *   Delta from the faulting page, Delta_from_cand = k), for the k some
 *   active box admits.
 * Positive candidates' pages go to req->addrs[] (any VMA). All user memory
 * is read with copy_from_user_nofault(): "resident" = readable without a
 * fault.
 *
 * Trace mode (trace_mb=N at load, trace=1): a probe on the swapin_fault
 * tracepoint records, for each swap-in fault of target_tgid, the fault and
 * (trace_stack=1) the same stack-anchor lines as eden's tracer
 * (rmem/handler.c, FLTRACE_STACK_ANCHORS: Stk/StkObj/StkObj1), scanning up
 * to 1024 stack words above SP, as binary records in an N MB buffer read
 * from /sys/kernel/debug/eden_swappf/trace (format: struct swpf_rec below;
 * tools/fltrace/swapin_trace_to_eden.py --records converts it). Pointers
 * into the faulting thread's stack VMA are ignored, as eden ignored all
 * non-uffd memory.
 *
 * Trees come from tools/fltrace/single_tree_codegen/gen_single_tree_code.py
 * --integer_bounds (no floating point); the stack header also needs
 * stack_anchor_slots.py's per-PC slots.
 *
 * Parameters (/sys/module/eden_swappf/parameters/, writable at runtime):
 *   mode         0 = off (the kernel's own read-ahead), 1 = trees,
 *                2 = read only the faulting page (no-prefetch baseline)
 *   target_tgid  the process to act on (and trace); every other process
 *                gets the kernel's own read-ahead
 *   spatial      mode 1: run the spatial stage (default 1)
 *   stack        mode 1: run the stack stage (default 0)
 *   next_n       spatial: pages +1..+next_n asked of the trees (default 4)
 *   direct_map   0 = predicted spatial pages only go into the swap cache,
 *                1 = the fault also maps them (sync), 2 = a worker maps them
 *   trace, trace_stack  trace mode on/off, with stack-anchor records
 *   trace_mb     (load time only) trace buffer size, MB; 0 = no tracing
 * Counters (read-only): faults, model_faults, predicted_pages, stack_faults
 * (faults with a stack tree), stack_anchors, stack_candidates,
 * stack_predicted (addrs[] entries asked for), trace_records,
 * trace_bytes, trace_dropped (faults not recorded: buffer full),
 * trace_truncated (faults with records cut at TREC_MAX); mapped_pages,
 * map_skipped, addr_read, addr_lock_skipped (kernel-wide, since boot).
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sched/clock.h>
#include <linux/debugfs.h>
#include <linux/swap_prefetch.h>
#include <linux/vmstat.h>
#include <linux/log2.h>
#include <linux/tracepoint.h>
#include <trace/events/swapin.h>
#include <asm/ptrace.h>

#include "native_tree_model_spatial.h"
#include "native_tree_model_stack.h"
#include "native_tree_eval.h"

#ifndef ST_STACK_ANCHOR_SLOTS		/* stack header without stack_anchor_slots.py */
static inline int st_stack_anchor_slots(uint64_t pc, const int16_t **slots)
{
	return 0;
}
#endif

#if !defined(ST_INTEGER_BOUNDS)
#error "generate the trees with gen_single_tree_code.py --integer_bounds (no floating point in the kernel)"
#endif

static int mode;
module_param(mode, int, 0644);
MODULE_PARM_DESC(mode, "0 = off (kernel read-ahead), 1 = trees, 2 = faulting page only, 3 = Leap");

static int target_tgid;
module_param(target_tgid, int, 0644);
MODULE_PARM_DESC(target_tgid, "process (tgid) to act on");

/* a second process to act on (co-run experiments); 0 = none. Not traced. */
static int target_tgid2;
module_param(target_tgid2, int, 0644);
MODULE_PARM_DESC(target_tgid2, "second process (tgid) to act on, 0 = none");

static inline bool is_target(void)
{
	int t2 = READ_ONCE(target_tgid2);

	return current->tgid == READ_ONCE(target_tgid) || (t2 && current->tgid == t2);
}

static int spatial = 1;
module_param(spatial, int, 0644);
MODULE_PARM_DESC(spatial, "mode 1: run the spatial stage");

static int stack;
module_param(stack, int, 0644);
MODULE_PARM_DESC(stack, "mode 1: run the stack-anchor stage");

static int stack_depth1 = 1;
module_param(stack_depth1, int, 0644);
MODULE_PARM_DESC(stack_depth1, "stack stage: also follow anchor-window pointers one hop (depth-1 candidates)");

static int next_n = 4;
module_param(next_n, int, 0644);
MODULE_PARM_DESC(next_n, "spatial: deltas +1..+next_n asked of the trees (1..64; match the training window)");

static int direct_map;
module_param(direct_map, int, 0644);
MODULE_PARM_DESC(direct_map, "0 = swap cache only, 1 = also map (sync), 2 = also map (async worker)");

static int trace;
module_param(trace, int, 0644);
MODULE_PARM_DESC(trace, "record target_tgid's swap-in faults (needs trace_mb)");

static int trace_depth = 1;
module_param(trace_depth, int, 0644);
MODULE_PARM_DESC(trace_depth, "trace: stack-anchor hops to follow (1 = StkObj1 only; up to 3)");

static int trace_stack = 1;
module_param(trace_stack, int, 0644);
MODULE_PARM_DESC(trace_stack, "trace: also record stack-anchor records");

static int trace_mb;
module_param(trace_mb, int, 0444);

/* time_faults=N: time the stack and spatial stages (local_clock) on the target's
 * next N faults in mode 1; writing it restarts the window (resets the t_* counters) */
static atomic64_t t_faults, t_stack_ns, t_spatial_ns, t_stack_faults, t_stack_hit_ns,
		  t_stack_cands, t_stack_max_ns;
static long time_faults;
static int set_time_faults(const char *val, const struct kernel_param *kp)
{
	int r = param_set_long(val, kp);

	atomic64_set(&t_faults, 0); atomic64_set(&t_stack_ns, 0); atomic64_set(&t_spatial_ns, 0);
	atomic64_set(&t_stack_faults, 0); atomic64_set(&t_stack_hit_ns, 0);
	atomic64_set(&t_stack_cands, 0); atomic64_set(&t_stack_max_ns, 0);
	return r;
}
static const struct kernel_param_ops time_faults_ops = { .set = set_time_faults, .get = param_get_long };
module_param_cb(time_faults, &time_faults_ops, &time_faults, 0644);
MODULE_PARM_DESC(time_faults, "time the stages on the next N target faults (0 = off)");
MODULE_PARM_DESC(trace_mb, "trace buffer size in MB (load time; 0 = no tracing)");

static atomic64_t faults = ATOMIC64_INIT(0);		/* target swap-in faults seen */
static atomic64_t model_faults = ATOMIC64_INIT(0);	/* ... whose PC has a spatial tree */
static atomic64_t predicted_pages = ATOMIC64_INIT(0);	/* spatial pages asked for */
static atomic64_t stack_faults = ATOMIC64_INIT(0);	/* ... whose PC has a stack tree + slots */
static atomic64_t stack_anchors = ATOMIC64_INIT(0);
static atomic64_t stack_candidates = ATOMIC64_INIT(0);
static atomic64_t stack_predicted = ATOMIC64_INIT(0);	/* addrs[] entries asked for */
static atomic64_t trace_records = ATOMIC64_INIT(0);
static atomic64_t trace_dropped = ATOMIC64_INIT(0);
static atomic64_t trace_truncated = ATOMIC64_INIT(0);

static int get_counter(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%lld\n", (long long)atomic64_read((atomic64_t *)kp->arg));
}
static const struct kernel_param_ops counter_ops = { .get = get_counter };
module_param_cb(faults, &counter_ops, &faults, 0444);
module_param_cb(model_faults, &counter_ops, &model_faults, 0444);
module_param_cb(predicted_pages, &counter_ops, &predicted_pages, 0444);
module_param_cb(stack_faults, &counter_ops, &stack_faults, 0444);
module_param_cb(stack_anchors, &counter_ops, &stack_anchors, 0444);
module_param_cb(stack_candidates, &counter_ops, &stack_candidates, 0444);
module_param_cb(stack_predicted, &counter_ops, &stack_predicted, 0444);
module_param_cb(trace_records, &counter_ops, &trace_records, 0444);
module_param_cb(trace_dropped, &counter_ops, &trace_dropped, 0444);
module_param_cb(trace_truncated, &counter_ops, &trace_truncated, 0444);
module_param_cb(t_faults, &counter_ops, &t_faults, 0444);
module_param_cb(t_stack_ns, &counter_ops, &t_stack_ns, 0444);
module_param_cb(t_spatial_ns, &counter_ops, &t_spatial_ns, 0444);
module_param_cb(t_stack_faults, &counter_ops, &t_stack_faults, 0444);	/* timed faults that ran a stack tree */
module_param_cb(t_stack_hit_ns, &counter_ops, &t_stack_hit_ns, 0444);	/* stack ns on those */
module_param_cb(t_stack_cands, &counter_ops, &t_stack_cands, 0444);
module_param_cb(t_stack_max_ns, &counter_ops, &t_stack_max_ns, 0444);

static int get_long_counter(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%ld\n", atomic_long_read((atomic_long_t *)kp->arg));
}
static const struct kernel_param_ops long_counter_ops = { .get = get_long_counter };
module_param_cb(mapped_pages, &long_counter_ops, &swap_pf_mapped_pages, 0444);
module_param_cb(map_skipped, &long_counter_ops, &swap_pf_map_skipped, 0444);
module_param_cb(addr_read, &long_counter_ops, &swap_pf_addr_read, 0444);
module_param_cb(addr_lock_skipped, &long_counter_ops, &swap_pf_addr_lock_skipped, 0444);

/* ---- user memory, read without faulting ---- */

#ifndef STK_REL				/* wider trace builds: make KCFLAGS=-DSTK_REL=512 */
#define STK_REL		64			/* bytes either side of a target */
#endif
/* trace records hold rel/prel in an s8: bytes for the default +-64 window,
 * 8-byte slots for wider ones (swapin_trace_to_eden.py --rel-slots) */
#define STK_REC_REL(bytes)	(STK_REL > 64 ? (bytes) / 8 : (bytes))
_Static_assert(STK_REL <= 64 || STK_REL / 8 <= 127, "rel in slots must fit in s8");
#define STK_HALF	(STK_REL / 8)		/* 8 slots */
#define STK_WIN		(2 * STK_HALF + 1)	/* w[STK_HALF + r] = slot r */
#define STK_MAX_WORDS	1024			/* trace: words scanned above SP */
#define STK_MIN_ADDR	0x10000UL

struct stk_range {
	unsigned long lo, hi;			/* the faulting thread's stack VMA */
};

/* the stack VMA holding sp. Called in the faulting task with preemption
 * (tracepoint) or rcu_read_lock() (predict) held: VMAs are RCU-freed with
 * CONFIG_PER_VMA_LOCK, so its bounds can be read without the mmap lock (a
 * VMA changing meanwhile only makes the filter below approximate) */
static bool stk_vma(unsigned long sp, struct stk_range *s)
{
	struct vm_area_struct *vma;

	if (!current->mm)
		return false;
	vma = vma_lookup(current->mm, sp);
	if (!vma)
		return false;
	s->lo = READ_ONCE(vma->vm_start);
	s->hi = READ_ONCE(vma->vm_end);
	return s->lo <= sp && sp < s->hi;
}

/* a pointer worth following: a user address outside the stack (eden's
 * tracer only followed pointers into uffd regions, i.e. the heap) */
static inline bool stk_ptr(unsigned long v, const struct stk_range *s)
{
	return v >= STK_MIN_ADDR && v < TASK_SIZE_MAX && !(v >= s->lo && v < s->hi);
}

static inline bool stk_resident(unsigned long v)
{
	u8 b;

	return !copy_from_user_nofault(&b, (const void __user *)v, 1);
}

/* the slots within +-64 bytes of addr's slot, clipped to its page, into
 * w[STK_HALF + r] for r in [*lo, *hi] (slots); false if not resident */
static bool stk_window(unsigned long addr, u64 *w, int *lo, int *hi)
{
	unsigned long slot = addr & ~7UL, page = addr & PAGE_MASK;
	long l = max(-(long)STK_REL, (long)page - (long)slot);
	long h = min((long)STK_REL, (long)(page + PAGE_SIZE - 8) - (long)slot);

	*lo = l / 8;
	*hi = h / 8;
	return !copy_from_user_nofault(&w[STK_HALF + *lo], (const void __user *)(slot + l),
				       h - l + 8);
}

/* ---- trace mode ---- */

/*
 * One 32-byte record; a fault's records are contiguous in the buffer, its
 * REC_FAULT first. rel/prel are bytes (multiples of 8 in -64..64).
 *   REC_FAULT   u32 = tgid, a = address, b = ip, c = sp, flag = in swap cache
 *   REC_STK     u32 = soff (words above SP), a = stack address, b = target,
 *               rel = matching slot - target
 *   REC_OBJ     a = target, c = slot value, rel = slot - target, flag = present
 *   REC_OBJ1    a = target, b = child (value of the anchor slot at prel),
 *               c = slot value, rel = slot - child, flag = present
 *   REC_OBJN    (trace_depth >= 2) depth d = 2..3: a = target, b = the
 *               object whose window this is, c = slot value, rel = slot -
 *               object, prel = anchor slot followed (hop 1), u32 = d |
 *               (u8)p2 << 8 | (u8)p3 << 16 with p2/p3 the slots followed at
 *               hops 2/3 (always in slots; p3 = 0 at depth 2), flag = present
 */
enum { REC_FAULT = 1, REC_STK, REC_OBJ, REC_OBJ1, REC_OBJN };
struct swpf_rec {
	u8 type, flag;
	s8 rel, prel;
	u32 u32;
	u64 a, b, c;
};

#define TREC_MAX	32768		/* records per fault */
#define T_MAX_TARGETS	64
#define T_MAX_CHILDREN	256
#define T_MAX_DEEP	1024		/* depth >= 2 objects visited per fault (BFS) */
#define T_SEEN		4096		/* visited-object hash (power of 2) */

struct swpf_deep {			/* an object whose window gives depth-d records */
	u64 obj;
	s8 p1, p2, p3;
	u8 d;
};

struct swpf_tcpu {			/* per-CPU scratch (probe runs with preemption off) */
	u64 stack[STK_MAX_WORDS];
	unsigned long targets[T_MAX_TARGETS], children[T_MAX_CHILDREN];
	u64 w0[STK_WIN], w1[STK_WIN];		/* anchor / child windows (kept off the stack) */
	struct swpf_deep deep[T_MAX_DEEP];
	unsigned long deep_target[T_MAX_DEEP];	/* anchor target of each queued object */
	int ndeep;
	u64 seen_key[T_SEEN];
	u32 seen_gen[T_SEEN], gen;
	int n;
	struct swpf_rec rec[TREC_MAX];
};

static char *tbuf;
static size_t tsize, tused;
static DEFINE_SPINLOCK(tlock);
static struct swpf_tcpu **tcpu;
static struct dentry *tdir;

static int get_trace_bytes(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%zu\n", READ_ONCE(tused));
}
static const struct kernel_param_ops trace_bytes_ops = { .get = get_trace_bytes };
module_param_cb(trace_bytes, &trace_bytes_ops, NULL, 0444);

static inline struct swpf_rec *trec(struct swpf_tcpu *t, int type)
{
	struct swpf_rec *r;

	if (t->n >= TREC_MAX)
		return NULL;
	r = &t->rec[t->n++];
	memset(r, 0, sizeof(*r));
	r->type = type;
	return r;
}

/* first visit of obj in this fault? (open-addressed set, reset by t->gen) */
static bool trace_first_visit(struct swpf_tcpu *t, unsigned long obj)
{
	u32 h = (u32)(hash_64(obj, 32)) & (T_SEEN - 1);
	int k;

	for (k = 0; k < T_SEEN; k++, h = (h + 1) & (T_SEEN - 1)) {
		if (t->seen_gen[h] != t->gen) {
			t->seen_gen[h] = t->gen;
			t->seen_key[h] = obj;
			return true;
		}
		if (t->seen_key[h] == obj)
			return false;
	}
	return false;
}

/* queue a resident object for depth d (2..3) records */
static void trace_deep_push(struct swpf_tcpu *t, unsigned long obj, int d, int p1, int p2, int p3)
{
	struct swpf_deep *q;

	if (d > READ_ONCE(trace_depth) || t->ndeep >= T_MAX_DEEP || !trace_first_visit(t, obj))
		return;
	q = &t->deep[t->ndeep++];
	q->obj = obj;
	q->d = d;
	q->p1 = p1;
	q->p2 = p2;
	q->p3 = p3;
}

/* depths 2..trace_depth, breadth first: each queued object's +-STK_REL
 * window; resident pointers there queue the next depth */
static void trace_deep(struct swpf_tcpu *t, unsigned long fpage, const struct stk_range *s,
		       const unsigned long *qtarget)
{
	u64 *w = t->w1;
	struct swpf_rec *r;
	int qi, lo, hi, i;

	for (qi = 0; qi < t->ndeep; qi++) {
		struct swpf_deep q = t->deep[qi];

		if ((q.obj & PAGE_MASK) == fpage || !stk_window(q.obj, w, &lo, &hi))
			continue;
		for (i = lo; i <= hi; i++) {
			unsigned long v = w[STK_HALF + i];

			if (!v || !stk_ptr(v, s) || !(r = trec(t, REC_OBJN)))
				continue;
			r->a = qtarget[qi];
			r->b = q.obj;
			r->c = v;
			r->rel = STK_REC_REL(i * 8);
			r->prel = STK_REC_REL(q.p1 * 8);
			r->u32 = q.d | (u32)(u8)q.p2 << 8 | (u32)(u8)q.p3 << 16;
			r->flag = stk_resident(v);
			if (r->flag && q.d < 3 && t->ndeep < T_MAX_DEEP) {
				t->deep_target[t->ndeep] = qtarget[qi];
				trace_deep_push(t, v, q.d + 1, q.p1, q.p2, i);
			}
		}
	}
}

static void trace_obj1(struct swpf_tcpu *t, unsigned long target, int prel, unsigned long child,
		       unsigned long fpage, const struct stk_range *s, int *nchildren)
{
	u64 *w = t->w1;
	struct swpf_rec *r;
	int k, lo, hi, i;

	if ((child & PAGE_MASK) == fpage)
		return;
	for (k = 0; k < *nchildren && t->children[k] != child; k++)
		;
	if (k < *nchildren || *nchildren >= T_MAX_CHILDREN)
		return;
	if (!stk_window(child, w, &lo, &hi))
		return;
	t->children[(*nchildren)++] = child;
	for (i = lo; i <= hi; i++) {
		unsigned long v = w[STK_HALF + i];

		if (!v || !stk_ptr(v, s) || !(r = trec(t, REC_OBJ1)))
			continue;
		r->a = target;
		r->b = child;
		r->c = v;
		r->rel = STK_REC_REL(i * 8);
		r->prel = STK_REC_REL(prel);
		r->flag = stk_resident(v);
		if (r->flag && READ_ONCE(trace_depth) >= 2 && t->ndeep < T_MAX_DEEP) {
			t->deep_target[t->ndeep] = target;
			trace_deep_push(t, v, 2, prel / 8, i, 0);
		}
	}
}

static void trace_stack_anchors(struct swpf_tcpu *t, unsigned long sp, unsigned long fpage)
{
	struct stk_range s;
	unsigned long end, a, val, page;
	u64 *w = t->w0;
	int nwords = 0, soff, i, k, lo, hi, ntargets = 0, nchildren = 0;
	struct swpf_rec *r;
	bool matched;

	if (!sp || !stk_vma(sp, &s))
		return;
	t->ndeep = 0;
	if (++t->gen == 0) {			/* wrapped: forget all stamps */
		memset(t->seen_gen, 0, sizeof(t->seen_gen));
		t->gen = 1;
	}
	end = min(s.hi, sp + STK_MAX_WORDS * 8);
	/* page by page, stopping at the first unreadable one */
	for (a = sp; a < end; ) {
		unsigned long n = min(end, (a & PAGE_MASK) + PAGE_SIZE) - a;

		if (copy_from_user_nofault((u8 *)t->stack + (a - sp), (const void __user *)a, n))
			break;
		a += n;
	}
	nwords = (a - sp) / 8;

	for (soff = 0; soff < nwords; soff++) {
		val = t->stack[soff];
		page = val & PAGE_MASK;
		if (!stk_ptr(val, &s) || page == fpage || !stk_window(val, w, &lo, &hi))
			continue;
		matched = false;
		for (i = lo; i <= hi; i++) {
			if ((w[STK_HALF + i] & PAGE_MASK) != fpage || !(r = trec(t, REC_STK)))
				continue;
			r->u32 = soff;
			r->a = sp + soff * 8;
			r->b = val;
			r->rel = STK_REC_REL(i * 8);
			matched = true;
		}
		if (!matched)
			continue;
		for (k = 0; k < ntargets && t->targets[k] != val; k++)
			;
		if (k < ntargets || ntargets >= T_MAX_TARGETS)
			continue;
		t->targets[ntargets++] = val;
		for (i = lo; i <= hi; i++) {
			unsigned long v = w[STK_HALF + i];

			if (!v || !stk_ptr(v, &s) || !(r = trec(t, REC_OBJ)))
				continue;
			r->a = val;
			r->c = v;
			r->rel = STK_REC_REL(i * 8);
			r->flag = stk_resident(v);
			if (r->flag)
				trace_obj1(t, val, i * 8, v, fpage, &s, &nchildren);
		}
	}
	if (t->ndeep)
		trace_deep(t, fpage, &s, t->deep_target);
}

static void swpf_trace_probe(void *data, unsigned long address, unsigned long ip,
			     unsigned long sp, bool in_swap_cache)
{
	struct swpf_tcpu *t;
	struct swpf_rec *r;
	size_t len, off = 0;
	bool ok = false;

	if (!READ_ONCE(trace) || !tbuf || current->tgid != READ_ONCE(target_tgid))
		return;
	t = tcpu[smp_processor_id()];
	t->n = 0;
	r = trec(t, REC_FAULT);
	r->u32 = current->tgid;
	r->a = address;
	r->b = ip;
	r->c = sp;
	r->flag = in_swap_cache;
	if (READ_ONCE(trace_stack))
		trace_stack_anchors(t, sp, address & PAGE_MASK);
	if (t->n >= TREC_MAX)
		atomic64_inc(&trace_truncated);

	len = t->n * sizeof(struct swpf_rec);
	spin_lock(&tlock);
	if (tused + len <= tsize) {
		off = tused;
		WRITE_ONCE(tused, tused + len);
		ok = true;
	}
	spin_unlock(&tlock);
	if (!ok) {
		atomic64_inc(&trace_dropped);
		return;
	}
	memcpy(tbuf + off, t->rec, len);
	atomic64_add(t->n, &trace_records);
}

/* the records written so far; read after the traced run (a fault being
 * recorded right now may not have its bytes copied in yet) */
static ssize_t trace_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, count, ppos, tbuf, READ_ONCE(tused));
}

static const struct file_operations trace_fops = {
	.owner = THIS_MODULE,
	.read = trace_read,
	.llseek = default_llseek,
};

static void trace_free(void)
{
	int cpu;

	if (tcpu) {
		for_each_possible_cpu(cpu)
			vfree(tcpu[cpu]);
		kfree(tcpu);
		tcpu = NULL;
	}
	vfree(tbuf);
	tbuf = NULL;
}

static int trace_init(void)
{
	int cpu;

	if (trace_mb <= 0)
		return 0;
	tsize = (size_t)trace_mb << 20;
	tbuf = vmalloc(tsize);
	tcpu = kcalloc(nr_cpu_ids, sizeof(*tcpu), GFP_KERNEL);
	if (!tbuf || !tcpu)
		goto nomem;
	for_each_possible_cpu(cpu)
		if (!(tcpu[cpu] = vmalloc(sizeof(struct swpf_tcpu))))
			goto nomem;
	tdir = debugfs_create_dir("eden_swappf", NULL);
	debugfs_create_file("trace", 0400, tdir, NULL, &trace_fops);
	return register_trace_swapin_fault(swpf_trace_probe, NULL);
nomem:
	trace_free();
	return -ENOMEM;
}

/* ---- prediction ---- */

/* history features over each target's swap-in faults: one slot per target
 * (target_tgid -> slot 0, target_tgid2 -> slot 1), so co-running targets do
 * not reset each other's history */
struct pf_hist {
	int tgid;
	bool have_last;
	unsigned long last_page, last_pc;
	long last_delta;		/* in pages */
};
static DEFINE_SPINLOCK(hist_lock);
static struct pf_hist hist[2];

static void stk_add(struct swap_prefetch_req *req, unsigned long page)
{
	int k;

	for (k = 0; k < req->naddr && req->addrs[k] != page; k++)
		;
	if (k == req->naddr && req->naddr < SWAP_PF_MAX_ADDRS) {
		req->addrs[req->naddr++] = page;
		atomic64_inc(&stack_predicted);
	}
}

static inline long stk_delta(unsigned long v, unsigned long fpage)
{
	return ((long)(v & PAGE_MASK) - (long)fpage) / (long)PAGE_SIZE;
}

/* pages past a stack candidate C scored as C+k (Delta_from_cand = k); must
 * match the labeller's PF_STACK_EXTENT (process_linux_trace.py, default 4) */
#define STK_EXTENT	4

/* a candidate slot q (holding v) at this depth: its page C and C+k for the
 * k in kmask (some active box admits them) */
static void stk_score(const st_model_t *m, uint32_t active, uint32_t kmask, unsigned long q,
		      int rel, unsigned long v, unsigned long fpage, int depth, int prel,
		      struct swap_prefetch_req *req)
{
	unsigned long cpage = v & PAGE_MASK;
	int k;

	for (k = 0; k <= STK_EXTENT; k++) {
		unsigned long page = cpage + k * PAGE_SIZE;

		if (!((kmask >> k) & 1u) || page == fpage || page >= TASK_SIZE_MAX)
			continue;
		atomic64_inc(&stack_candidates);
		if (st_predict_x(m, active, (q & ~PAGE_MASK) / 8, rel, stk_delta(page, fpage),
				 depth, prel, k))
			stk_add(req, page);
	}
}

/* the slots within +-64B of child, at depth 1 through anchor slot prel */
static void stk_depth1(const st_model_t *m, uint32_t active, uint32_t kmask, unsigned long child, int prel,
		       unsigned long fpage, const struct stk_range *s,
		       struct swap_prefetch_req *req)
{
	u64 w[STK_WIN];
	int lo, hi, i;

	if ((child & PAGE_MASK) == fpage || !stk_window(child, w, &lo, &hi))
		return;
	for (i = lo; i <= hi; i++) {
		unsigned long v = w[STK_HALF + i];
		unsigned long q = (child & ~7UL) + i * 8;

		/* C == the faulting page still gives C+k (stk_score skips k = 0) */
		if (!v || !stk_ptr(v, s))
			continue;
		stk_score(m, active, kmask, q, i, v, fpage, 1, prel, req);
	}
}

static bool stk_follow_possible(const st_model_t *m, uint32_t active, int prel)
{
	int b;

	for (b = 0; b < m->nboxes; b++)
		if ((active >> b) & 1u && st_in(1, m->boxes[b].dep_lo, m->boxes[b].dep_hi) &&
		    st_in(prel, m->boxes[b].par_lo, m->boxes[b].par_hi))
			return true;
	return false;
}

static void stack_stage(unsigned long pc, unsigned long prev_pc, long prev_delta,
			unsigned long fpage, struct swap_prefetch_req *req)
{
	unsigned long sp = user_stack_pointer(task_pt_regs(current)), val;
	const int16_t *slots;
	struct stk_range s;
	st_model_t model;
	uint32_t active, kmask;
	u64 w[STK_WIN];
	int nslots, si, i, lo, hi;

	if (!st_stack_lookup(pc, &model) || !(nslots = st_stack_anchor_slots(pc, &slots)))
		return;
	if (!(active = model.fault(pc, prev_pc, prev_delta)) || !sp || !stk_vma(sp, &s))
		return;
	atomic64_inc(&stack_faults);
	kmask = st_cand_mask(&model, active, STK_EXTENT);
	for (si = 0; si < nslots; si++) {
		unsigned long a = sp + slots[si] * 8;
		bool anchored = false;

		if (slots[si] < 0 || a + 8 > s.hi ||
		    copy_from_user_nofault(&val, (const void __user *)a, 8))
			continue;
		if (!stk_ptr(val, &s) || (val & PAGE_MASK) == fpage || !stk_window(val, w, &lo, &hi))
			continue;
		for (i = lo; i <= hi && !anchored; i++)
			anchored = (w[STK_HALF + i] & PAGE_MASK) == fpage;
		if (!anchored)
			continue;
		atomic64_inc(&stack_anchors);
		for (i = lo; i <= hi; i++) {
			unsigned long v = w[STK_HALF + i];
			unsigned long q = (val & ~7UL) + i * 8;

			if (!v || !stk_ptr(v, &s))
				continue;
			stk_score(&model, active, kmask, q, i, v, fpage, 0, 0, req);
			if (READ_ONCE(stack_depth1) && (v & PAGE_MASK) != fpage &&
			    stk_follow_possible(&model, active, i))
				stk_depth1(&model, active, kmask, v, i, fpage, &s, req);
		}
	}
}

/* ---- Leap (mode 3) ----
 * Leap's majority-trend prefetcher (Al Maruf & Chowdhury, ATC '20, Algorithms
 * 1 and 2), per target process over the virtual page numbers of its swap-in
 * faults, as the paper describes (process-specific ACCESS HISTORY of fault
 * deltas). Defaults from the paper's evaluation: Hsize = 32, PWsize_max = 8.
 * Trend detection starts at a window of Hsize / 4 and doubles up to Hsize
 * (as Leap's released find_trend()); a delta is the majority if it appears
 * more than w/2 times in the newest w deltas (Boyer-Moore + verification).
 * C_hit = readahead-flagged swap-cache hits since the previous fault (the
 * kernel's SWAP_RA_HIT event, summed over CPUs; Leap also used a global hit
 * counter). Window (GetPrefetchWindowSize): C_hit = 0 -> 1 page if this fault
 * follows the current trend, else 0; otherwise C_hit + 1 rounded up to a power
 * of 2; capped at PWsize_max, shrinking at most by half per fault. With a
 * trend, PW pages at P + k*delta (k = 1..PW); without one, PW pages around P
 * along the latest trend found (k = -PW/2..-1 and 1..PW - PW/2), +1 if none
 * was ever found. Pages go to req->addrs[] (any VMA, not limited to the
 * fault's 2MB page table). A majority delta of 0 is not a trend. */
/* leap_variant=1: the window and fallback of Leap's released kernel code
 * (SymbioticLab/Leap mm/swap_state.c, swapin_nr_pages() with custom prefetch
 * on): pages = hits + 2 rounded up to a power of 2 starting at 4, capped at
 * PWsize_max, shrinking at most by half; it never suspends. With a trend it
 * reads pages - 1 pages at P + k*delta (k = 1..pages-1); without one, the
 * aligned block of `pages` pages around P (Linux's cluster read-ahead, here
 * over virtual pages). Trend detection is the same in both variants. */
static int leap_variant;
module_param(leap_variant, int, 0644);
MODULE_PARM_DESC(leap_variant, "mode 3: 0 = Leap paper's window (Algorithm 2), 1 = Leap's released code (window >= 4, aligned-block fallback)");

#define LEAP_HSIZE	32
#define LEAP_PW_MAX	8
#define LEAP_W0		(LEAP_HSIZE / 4)

struct leap_state {
	int tgid;
	bool have_last, have_trend;
	unsigned long last_page;	/* virtual page number */
	long trend;			/* latest majority delta, in pages */
	long d[LEAP_HSIZE];		/* fault deltas, ring, newest at head - 1 */
	int head, size;
	unsigned int pw_prev;
	unsigned long hits_seen;
};
static struct leap_state leap[2];
static DEFINE_SPINLOCK(leap_lock);

static unsigned long leap_ra_hits(void)
{
	unsigned long s = 0;
	int cpu;

	for_each_online_cpu(cpu)
		s += per_cpu(vm_event_states, cpu).event[SWAP_RA_HIT];
	return s;
}

static inline long leap_delta(const struct leap_state *l, int i)	/* i-th newest */
{
	return l->d[(l->head - 1 - i + LEAP_HSIZE) % LEAP_HSIZE];
}

static bool leap_majority(const struct leap_state *l, int w, long *maj)
{
	long cand = 0;
	int i, cnt = 0;

	for (i = 0; i < w; i++) {
		long v = leap_delta(l, i);

		if (cnt == 0) {
			cand = v;
			cnt = 1;
		} else if (v == cand) {
			cnt++;
		} else {
			cnt--;
		}
	}
	for (i = 0, cnt = 0; i < w; i++)
		cnt += leap_delta(l, i) == cand;
	*maj = cand;
	return cnt > w / 2 && cand != 0;
}

static bool leap_find_trend(const struct leap_state *l, long *maj)
{
	int w;

	for (w = LEAP_W0; w <= LEAP_HSIZE && w <= l->size; w *= 2)
		if (leap_majority(l, w, maj))
			return true;
	return false;
}

static bool leap_predict(struct vm_fault *vmf, struct swap_prefetch_req *req)
{
	unsigned long page = vmf->real_address >> PAGE_SHIFT, hits, chit;
	struct leap_state *l;
	long delta, maj = 0, stride;
	unsigned int pw;
	bool found, code;
	int k, lo, hi;

	hits = leap_ra_hits();
	l = &leap[current->tgid == READ_ONCE(target_tgid) ? 0 : 1];
	spin_lock(&leap_lock);
	if (l->tgid != current->tgid) {		/* new target in this slot: fresh state */
		memset(l, 0, sizeof(*l));
		l->tgid = current->tgid;
		l->hits_seen = hits;
	}
	delta = l->have_last ? (long)page - (long)l->last_page : 0;
	l->d[l->head] = delta;
	l->head = (l->head + 1) % LEAP_HSIZE;
	if (l->size < LEAP_HSIZE)
		l->size++;
	l->last_page = page;
	l->have_last = true;

	found = leap_find_trend(l, &maj);
	if (found) {
		l->trend = maj;
		l->have_trend = true;
	}
	/* GetPrefetchWindowSize */
	chit = hits - l->hits_seen;
	l->hits_seen = hits;
	code = READ_ONCE(leap_variant) == 1;
	if (code)
		pw = max_t(unsigned int, 4, roundup_pow_of_two(chit + 2));
	else if (chit == 0)
		pw = (found && delta == maj) ? 1 : 0;
	else
		pw = roundup_pow_of_two(chit + 1);
	pw = min_t(unsigned int, pw, LEAP_PW_MAX);
	if (pw < l->pw_prev / 2)
		pw = l->pw_prev / 2;
	l->pw_prev = pw;
	stride = found ? maj : (l->have_trend ? l->trend : 1);
	spin_unlock(&leap_lock);

	atomic64_inc(&faults);
	if (found)
		atomic64_inc(&model_faults);
	if (!pw)
		return true;			/* faulting page only */
	/* DoPrefetch: along the trend, or around P with the latest trend */
	lo = found ? 1 : -(int)(pw / 2);
	hi = found ? (int)pw : (int)(pw - pw / 2);
	if (code) {
		if (found) {			/* pw pages incl. P along the trend */
			lo = 1;
			hi = (int)pw - 1;
		} else {			/* aligned block of pw pages around P */
			stride = 1;
			lo = -(int)(page & (pw - 1));
			hi = lo + (int)pw - 1;
		}
	}
	for (k = lo; k <= hi && req->naddr < SWAP_PF_MAX_ADDRS; k++) {
		long p = (long)page + k * stride;

		if (k == 0 || p <= 0 || (unsigned long)p >= (TASK_SIZE_MAX >> PAGE_SHIFT))
			continue;
		req->addrs[req->naddr++] = (unsigned long)p << PAGE_SHIFT;
	}
	atomic64_add(req->naddr, &predicted_pages);
	return true;
}

static bool eden_predict(struct vm_fault *vmf, struct swap_prefetch_req *req)
{
	unsigned long pc, page, prev_pc;
	long prev_delta;
	struct pf_hist *h;
	int fl, k, n, m = mode;
	st_model_t model;
	uint32_t active;
	bool timing;
	u64 t0 = 0, t1 = 0;
	s64 sf0 = 0, sc0 = 0;

	if (m == 0 || !is_target())
		return false;
	if (m == 2)
		return true;			/* empty req: faulting page only */
	if (m == 3)
		return leap_predict(vmf, req);

	pc = instruction_pointer(task_pt_regs(current));
	page = vmf->real_address & PAGE_MASK;
	fl = (vmf->real_address & ~PAGE_MASK) / sizeof(u64);

	h = &hist[current->tgid == READ_ONCE(target_tgid) ? 0 : 1];
	spin_lock(&hist_lock);
	if (h->tgid != current->tgid) {	/* new target in this slot: fresh history */
		h->tgid = current->tgid;
		h->have_last = false;
		h->last_pc = 0;
		h->last_delta = 0;
	}
	prev_pc = h->last_pc;
	prev_delta = h->last_delta;
	h->last_delta = h->have_last ? ((long)page - (long)h->last_page) / (long)PAGE_SIZE : 0;
	h->last_page = page;
	h->last_pc = pc;
	h->have_last = true;
	spin_unlock(&hist_lock);

	atomic64_inc(&faults);
	timing = READ_ONCE(time_faults) > 0 && atomic64_read(&t_faults) < READ_ONCE(time_faults);
	if (timing) {
		sf0 = atomic64_read(&stack_faults);
		sc0 = atomic64_read(&stack_candidates);
		t0 = local_clock();
	}
	/* stack first: like eden, stack candidates take their pages first */
	if (READ_ONCE(stack))
		stack_stage(pc, prev_pc, prev_delta, page, req);
	if (timing)
		t1 = local_clock();
	if (READ_ONCE(spatial) && st_spatial_lookup(pc, &model)) {
		atomic64_inc(&model_faults);
		active = model.fault(pc, prev_pc, prev_delta);
		n = clamp(READ_ONCE(next_n), 1, 64);
		for (k = 1; active && k <= n; k++)
			if (st_predict(&model, active, -1, -1 - fl, k))
				req->fwd |= 1ULL << (k - 1);
		atomic64_add(hweight64(req->fwd), &predicted_pages);
	}
	if (timing) {
		u64 t2 = local_clock(), ds = t1 - t0;
		s64 mx;

		atomic64_inc(&t_faults);
		atomic64_add(ds, &t_stack_ns);
		atomic64_add(t2 - t1, &t_spatial_ns);
		if (atomic64_read(&stack_faults) != sf0) {
			atomic64_inc(&t_stack_faults);
			atomic64_add(ds, &t_stack_hit_ns);
			atomic64_add(atomic64_read(&stack_candidates) - sc0, &t_stack_cands);
		}
		mx = atomic64_read(&t_stack_max_ns);
		while ((s64)ds > mx && !atomic64_try_cmpxchg(&t_stack_max_ns, &mx, ds))
			;
	}
	req->map = clamp(READ_ONCE(direct_map), SWAP_PF_MAP_NONE, SWAP_PF_MAP_ASYNC);
	return true;
}

static const struct swap_prefetch_ops eden_ops = {
	.predict = eden_predict,
};

static int __init eden_swappf_init(void)
{
	int ret = trace_init();

	if (ret)
		return ret;
	ret = register_swap_prefetcher(&eden_ops);
	if (ret) {
		if (tbuf) {
			unregister_trace_swapin_fault(swpf_trace_probe, NULL);
			tracepoint_synchronize_unregister();
			debugfs_remove_recursive(tdir);
		}
		trace_free();
		return ret;
	}
	pr_info("eden_swappf: registered (mode %d, target_tgid %d, trace buffer %d MB)\n",
		mode, target_tgid, trace_mb);
	return 0;
}

static void __exit eden_swappf_exit(void)
{
	unregister_swap_prefetcher(&eden_ops);
	if (tbuf) {
		unregister_trace_swapin_fault(swpf_trace_probe, NULL);
		tracepoint_synchronize_unregister();
		debugfs_remove_recursive(tdir);
	}
	trace_free();
	pr_info("eden_swappf: unregistered\n");
}

module_init(eden_swappf_init);
module_exit(eden_swappf_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("eden PySTreeD spatial + stack-anchor trees as a swap-in prefetcher, and a stack-anchor tracer");
