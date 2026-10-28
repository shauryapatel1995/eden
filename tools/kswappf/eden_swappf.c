// SPDX-License-Identifier: GPL-2.0
/*
 * eden_swappf - eden's PySTreeD spatial prefetcher as a Linux swap-in
 * prefetcher (kernel 6.6 "swappf": include/linux/swap_prefetch.h).
 *
 * On each swap-in fault of the target process, evaluates that fault PC's
 * spatial tree on pages +1..+4 and asks the kernel to read the predicted ones
 * together with the faulting page. Trees come from
 * tools/fltrace/single_tree_codegen/gen_single_tree_code.py --integer_bounds
 * (no floating point); the feature definitions match eden's (rmem/prefetch.c
 * page_prefetch_spatial()):
 *   PC         - the user-space PC of the fault
 *   prev_pc    - the PC of this process's previous swap-in fault
 *   prev_delta - page delta between its two previous swap-in faults
 *   offset = -1, offset_from_faulting = -1 - (8-byte slot of the fault in
 *   its page), delta = k for page +k
 *
 * Parameters (/sys/module/eden_swappf/parameters/, writable at runtime):
 *   mode         0 = off (the kernel's own read-ahead), 1 = spatial trees,
 *                2 = read only the faulting page (no-prefetch baseline)
 *   target_tgid  the process to act on; every other process gets the
 *                kernel's own read-ahead
 *   next_n       mode 1: pages +1..+next_n asked of the trees (default 4;
 *                set it to the PF_SPATIAL_N the trees were trained with)
 * Counters (read-only): faults, model_faults, predicted_pages.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/swap_prefetch.h>
#include <asm/ptrace.h>

#include "native_tree_model_spatial.h"
#include "native_tree_eval.h"

#if !defined(ST_INTEGER_BOUNDS)
#error "generate the trees with gen_single_tree_code.py --integer_bounds (no floating point in the kernel)"
#endif

static int mode;
module_param(mode, int, 0644);
MODULE_PARM_DESC(mode, "0 = off (kernel read-ahead), 1 = spatial trees, 2 = faulting page only");

static int target_tgid;
module_param(target_tgid, int, 0644);
MODULE_PARM_DESC(target_tgid, "process (tgid) to act on");

static int next_n = 4;
module_param(next_n, int, 0644);
MODULE_PARM_DESC(next_n, "mode 1: deltas +1..+next_n asked of the trees (1..64; match the training window)");

static atomic64_t faults = ATOMIC64_INIT(0);		/* target swap-in faults seen */
static atomic64_t model_faults = ATOMIC64_INIT(0);	/* ... whose PC has a tree */
static atomic64_t predicted_pages = ATOMIC64_INIT(0);	/* pages asked for */

static int get_counter(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%lld\n", (long long)atomic64_read((atomic64_t *)kp->arg));
}
static const struct kernel_param_ops counter_ops = { .get = get_counter };
module_param_cb(faults, &counter_ops, &faults, 0444);
module_param_cb(model_faults, &counter_ops, &model_faults, 0444);
module_param_cb(predicted_pages, &counter_ops, &predicted_pages, 0444);

/* history features over the target's swap-in faults (one target process) */
static DEFINE_SPINLOCK(hist_lock);
static int hist_tgid;
static bool have_last;
static unsigned long last_page, last_pc;
static long last_delta;			/* in pages */

static bool eden_predict(struct vm_fault *vmf, struct swap_prefetch_req *req)
{
	unsigned long pc, page, prev_pc;
	long prev_delta;
	int fl, k, n, m = mode;
	st_model_t model;
	uint32_t active;

	if (m == 0 || current->tgid != READ_ONCE(target_tgid))
		return false;
	if (m == 2)
		return true;			/* empty req: faulting page only */

	pc = instruction_pointer(task_pt_regs(current));
	page = vmf->real_address & PAGE_MASK;
	fl = (vmf->real_address & ~PAGE_MASK) / sizeof(u64);

	spin_lock(&hist_lock);
	if (hist_tgid != current->tgid) {	/* new target: start a fresh history */
		hist_tgid = current->tgid;
		have_last = false;
		last_pc = 0;
		last_delta = 0;
	}
	prev_pc = last_pc;
	prev_delta = last_delta;
	last_delta = have_last ? ((long)page - (long)last_page) / (long)PAGE_SIZE : 0;
	last_page = page;
	last_pc = pc;
	have_last = true;
	spin_unlock(&hist_lock);

	atomic64_inc(&faults);
	if (!st_spatial_lookup(pc, &model))
		return true;			/* no tree: just the faulting page */
	atomic64_inc(&model_faults);
	active = model.fault(pc, prev_pc, prev_delta);
	n = clamp(READ_ONCE(next_n), 1, 64);
	for (k = 1; active && k <= n; k++)
		if (st_predict(&model, active, -1, -1 - fl, k))
			req->fwd |= 1ULL << (k - 1);
	atomic64_add(hweight64(req->fwd), &predicted_pages);
	return true;
}

static const struct swap_prefetch_ops eden_ops = {
	.predict = eden_predict,
};

static int __init eden_swappf_init(void)
{
	int ret = register_swap_prefetcher(&eden_ops);

	if (!ret)
		pr_info("eden_swappf: registered (mode %d, target_tgid %d)\n", mode, target_tgid);
	return ret;
}

static void __exit eden_swappf_exit(void)
{
	unregister_swap_prefetcher(&eden_ops);
	pr_info("eden_swappf: unregistered\n");
}

module_init(eden_swappf_init);
module_exit(eden_swappf_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("eden PySTreeD spatial trees as a swap-in prefetcher");
