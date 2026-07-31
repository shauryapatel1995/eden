# Porting the Content-Directed Prefetcher into Eden

Source: `github.com/shauryapatel1995/fltrace`, branch `model_verification`
(diff against `origin/master` merge-base `2839d4f`): 20 files changed, ~1122
insertions / 40 deletions. `fltrace` is a smaller, standalone sibling of this
repo's remote-memory subsystem (it's structurally the same code as Eden's
`RMEM_STANDALONE` mode — see `tools/fltrace/`), which is why most of this
ports over almost directly rather than needing a redesign.

**Update**: the source repo's prediction backend is XGBoost
(`rmem/xgboost_prefetcher.c`), but that file is *not* going to be ported.
The plan below treats prediction as a swappable backend behind
`prefetch.h`'s three functions (`init_prefetcher()`, `page_prefetch_preds()`,
`page_postfetch_preds()`) — everything else in fltrace's design (`prefetch.c`,
the fault-path integration, the `FeatureVector` struct) calls only through
that seam and never references XGBoost directly, so the swap is clean
*as long as the feature set stays the same*. The replacement predictor (a
tree → C compiler) implementation and its exact function signature aren't
finalized yet — this doc marks the swap point rather than assuming its
shape. Also, the kernel patch needed for the `pc` feature (see below) is
being handled outside this plan by you directly.

This document is a plan only — no code has been changed in this repo.

## Status (updated after commit `6ee5028`, "Porting basic prefetching
implementation from fltrace")

**Done** — the LOCAL-backend port described below, plus full Shenango
runtime integration (not just the `RMEM_STANDALONE`/`tools/fltrace` build):
- New files: `inc/rmem/prefetch.h`, `rmem/prefetch.c`.
- Patched files: `inc/rmem/backend.h`, `inc/rmem/stats.h`/`rmem/stats.c`,
  `inc/rmem/fault.h`/`rmem/fault.c`, `rmem/handler.c` (**all** call sites
  updated, including the `#ifndef RMEM_STANDALONE` Shenango-stealing path),
  `rmem/common.c`, `rmem/rmem_local.c` (`local_post_read_prefetch` wired in).
- Build gating: `DO_PREFETCH` is a Makefile flag (`make fltrace.so
  DO_PREFETCH=1`), not a `config.h` `#define` — resolves the "gate behind a
  flag?" question in the Build System section below in favor of yes.
- `DO_RDAHEAD` is now unconditionally defined in `config.h` (always on);
  `DO_PREFETCH` stays opt-in via the Makefile.

**Deviation from plan** — `rmem/xgboost_prefetcher.c` was **not** skipped as
originally planned. It was ported essentially as-is into
`tools/fltrace/xgboost_prefetcher.c` (guarded by `#ifdef DO_PREFETCH`, so
it's a no-op / doesn't pull in `libxgboost` unless that flag is set) and is
what currently implements `init_prefetcher()`/`page_prefetch_preds()`/
`page_postfetch_preds()`. This is being used as a working interim backend;
the tree→C compiler swap described in "Swapping in the generated predictor"
below has **not** happened yet — that section is still forward-looking, not
history.

**Done — LOCAL backend end-to-end test, with the `pc` kernel feature
disabled** (`benchmarks/ll` linked-list microbenchmark, `models/random-ll-*`
model, `make fltrace.so DO_PREFETCH=1`, run under
`LD_PRELOAD=fltrace.so`). This required fixing several bugs found along the
way, none specific to the prefetcher port itself:
- `inc/rmem/common.h`: `RUNTIME_ENTER()`/`RUNTIME_EXIT()`/`IN_RUNTIME()`/
  `NOT_IN_RUNTIME()` had been deleted from the header by commit `12d0bd5`
  (Apr 2023) without updating `tools/fltrace/fltrace.c`/`stat.c`, which
  still called them — restored verbatim (undefined-symbol build break,
  present for 2+ years, never hit because `fltrace.so` was never linked).
- `inc/runtime/preempt.h` + `rmem/fsampler.c`: `preempt_disable()`/
  `preempt_enable()` used raw `%fs:preempt_cnt@tpoff` asm, which forces a
  TLS model invalid in a shared object; switched to plain C. Added a
  `RMEM_STANDALONE`-only `preempt_cnt` storage + `preempt()` stub, since the
  real ones (`runtime/preempt.c`) belong to the Shenango scheduler and
  aren't linked into `fltrace.so`.
- `rmem/handler.c`: `rmem_handler()` marked itself runtime-only via
  `preempt_disable()` but never called `RUNTIME_ENTER()` — a *separate*
  guard that `fltrace.c`'s malloc/mmap interposition actually checks. Without
  it, the handler thread's own setup allocation
  (`zero_page_init_thread()`'s `aligned_alloc()`) got treated as app memory,
  routed through jemalloc → `rmalloc()`, and deadlocked waiting on the very
  thread that would need to service the fault that routing created.
- The `pip install xgboost` wheel's `libxgboost.so` bundles CUDA/thrust
  support that's triggered on the *first* prediction call
  (`xgboost::common::AllVisibleGPUs()`) even for pure-CPU inference; on this
  GPU-less machine something in that bundled code deadlocks on a
  glibc-internal lock (`__exit_funcs_lock`, via a CUDA-error-category
  static's atexit registration) and never returns. Fixed by vendoring the
  `xgboost-cpu` PyPI wheel's `libxgboost.so` instead (no CUDA/thrust code
  compiled in at all — confirmed via `nm`/`ldd`) under
  `tools/fltrace/xgboost_cpu_lib/` (gitignored, not pip-installed, since
  `xgboost-cpu` shares the `xgboost` import name and would silently replace
  whatever's already pip-installed for other uses).
- `tools/fltrace/xgboost_prefetcher.c`'s `XGBOOST_LIB_PATH` (the runtime
  `dlopen()` target) was hardcoded to `/usr/local/lib/libxgboost.so` —
  made overridable via `EDEN_PREFETCH_XGBOOST_LIB_PATH`.
- XGBoost's OpenMP thread pool defaults to one thread per core; since
  `prefetch.c` calls inference with `batch_size=1` hundreds of times per
  fault, this must be capped (`OMP_NUM_THREADS=1` in the environment) or
  each tiny prediction pays for a full thread-pool spin-up.

Result: `faults_done` matched `faults` exactly (8412/8412, LOCAL backend,
4MB local / 64MB backing memory, 4096-node linked list) — no stuck faults,
clean exit. **`prefetched_pages` was 0** for this run — expected given `pc`
is unavailable (always 0) without the kernel patch, so the model's real
predictive feature is missing; this validates the pipeline mechanics (fault
→ candidate construction → inference call → completion), not the model's
hit rate.

**Not done — the RDMA backend still has no `post_read_prefetch`.**
Confirmed directly: `rmem/rmem_rdma.c`'s `rdma_backend_ops` struct has no
`post_read_prefetch` initializer (defaults to `NULL`), and `rmem/common.c`
now has an explicit guard —
`BUG_ON(rmbackend_type == RMEM_BACKEND_RDMA)` inside the `#ifdef
DO_PREFETCH` block in `rmem_common_init()` — so a `DO_PREFETCH` build
configured with the RDMA backend fails fast at startup instead of crashing
on the first prefetch. The "two things that need real design work" section
below, item 1, is unaddressed. **This means end-to-end prefetching over
RDMA does not run yet** — see the note at the end of this document.

**Not done — the `pc` kernel patch.** `fault.h`'s `fault_t.pc`/
`faulting_addr` fields and `handler.c`'s `read_uffd_fault()` plumbing are in
place, but gated behind a new `UFFD_PC_SUPPORTED` flag in `config.h` that is
left commented out (not defined) until the out-of-tree kernel patch lands.
Item 2 of "the two things that need real design work" is still open.

## What the prefetcher does

It's a **content-directed (pointer-chasing) prefetcher** driven by a binary
classifier (XGBoost in the source repo; a tree→C compiler's output in this
port — see "Swapping in the generated predictor" below), triggered *after*
a normally-faulted page is fully serviced (in `fault_done()`, gated by
`#ifdef DO_PREFETCH`):

1. Treats the just-fetched page as an array of 512 8-byte candidate pointers,
   plus 4 "next sequential page" candidates.
2. For each candidate, builds a `FeatureVector { pc, offset, delta,
   offset_from_faulting }` — `pc` is the faulting instruction's address,
   `delta` is the candidate pointer value's page distance from the base page.
3. Runs the candidate through the prediction backend (`page_postfetch_preds`);
   on a positive prediction, checks the target page isn't already
   present/locked (`is_page_prefetchable`), reads it into a scratch buffer
   via a new backend op (`post_read_prefetch`), and completes it exactly
   like a normal fault completion (`prefetch_read_done`: `uffd_copy` +
   page-node bookkeeping so it's evictable later).
4. Tracks how much memory this pulled in so eviction accounting
   (`nevicts_needed`) stays correct.

Note `page_prefetch_preds()` (a *pre*-fetch hook, called before page content
is available) is currently a stub returning 0 — the only active mechanism is
the post-fetch/pointer-chase path above.

## Compatibility check: how close are the two codebases?

Verified directly (not assumed) by diffing fltrace's pre-prefetcher base
against this repo's current files:

| File | Result |
|---|---|
| `rmem/fault.c` | **byte-identical** |
| `inc/rmem/backend.h` | **byte-identical** |
| `inc/rmem/stats.h` | **byte-identical** |
| `rmem/handler.c` | same core (`read_uffd_fault`, `rmem_handler`), Eden adds Shenango kthread-stealing code under `#ifndef RMEM_STANDALONE` |
| `rmem/common.c` | same core, Eden adds `RMEM_BACKEND_RDMA` registration + Shenango `__from_runtime` default |
| `rmem/rmem_local.c` | same structure/functions, trivial `reg->server` field difference |
| `inc/rmem/fault.h` | same `fault_t` layout *shape*, but Eden already uses the slot fltrace calls `unused3` for a Shenango `thread_t* thread` pointer instead |

Net effect: this is a clean port for everything except the RDMA backend
(fltrace has no RDMA backend at all — see below) and the two files where
Eden's Shenango integration lives.

## File-by-file plan

New files (copy in, then adapt per notes below):
- ✅ **Done** `inc/rmem/prefetch.h` → `inc/rmem/prefetch.h` (as-is, *unless* the tree→C
  compiler's output needs a different `FeatureVector` shape — see "Swapping
  in the generated predictor" below)
- ✅ **Done** `rmem/prefetch.c` → `rmem/prefetch.c` (as-is; only touches `is_page_prefetchable`, `prefetch_read_done`, `page_postfetch`, none of which reference Shenango or RDMA directly — they go through `rmbackend->post_read_prefetch`)
- ⚠️ **Done, but deviated from plan** `rmem/xgboost_prefetcher.c` → was
  going to be **not ported**, replaced by a new tree→C-compiler-backed
  file. Instead it was ported essentially as-is into
  `tools/fltrace/xgboost_prefetcher.c` (`#ifdef DO_PREFETCH`-gated) and is
  the backend currently in use. The tree→C swap in "Swapping in the
  generated predictor" below has not happened.

Patched files:
- ✅ **Done** `inc/rmem/backend.h`: add the `post_read_prefetch` function pointer to `struct rmem_backend_ops` (mirrors `post_read`).
- ✅ **Done** `inc/rmem/stats.h`, `rmem/stats.c`: add `RSTAT_FAULTS_REDUNDANT` and `RSTAT_PREFETCHES` counters + names — direct copy, no conflicts.
- ✅ **Done** `inc/rmem/fault.h`: add `faulting_addr`, `pc` (both `uint64_t`) to `fault_t`, plus padding — **do the math against Eden's actual layout, not fltrace's** (see below). Change `fault_done()` signature to `fault_done(fault_t *fault, int chan_id, int *nevicts_needed)` and add `prefetch_init()`/`is_page_prefetchable()`/`prefetch_read_done()` declarations.
- ✅ **Done** `rmem/fault.c`: add `prefetch_init()`, `is_page_prefetchable()`, `prefetch_alloc_page_nodes()`, `prefetch_read_done()`; extend `fault_done()` for the new signature and the `#ifdef DO_PREFETCH` call to `page_postfetch()`; extend `handle_page_fault()` with `RSTAT(FAULTS_REDUNDANT)++` and (optionally) the `#ifdef DO_RDAHEAD` gating fltrace applied to the existing read-ahead loop.
- ✅ **Done** `rmem/handler.c`: update **every** `fault_done(f)` call site to the new 3-arg form. Eden has more call sites than fltrace because of the Shenango-stealing path (`hthr_fault_read_steal_done` etc., which don't exist in fltrace) — grep for `fault_done(` in `rmem/handler.c` and update all of them, including inside the `#ifndef RMEM_STANDALONE` block fltrace's diff never touched (because fltrace can't compile that path at all).
- ✅ **Done** `rmem/common.c`: add the `prefetch_init()` call inside `rmem_common_init()`, after `eviction_init()` — same insertion point in both codebases. Also added a `BUG_ON(rmbackend_type == RMEM_BACKEND_RDMA)` guard not in the original plan (see RDMA section below).
- ✅ **Done** `rmem/rmem_local.c`: add `local_post_read_prefetch()` and wire it into `local_backend_ops.post_read_prefetch` — direct port.

## Swapping in the generated predictor

The seam is `inc/rmem/prefetch.h`'s three declared functions — `prefetch.c`
never calls XGBoost APIs itself, only these:

```c
void init_prefetcher();
unsigned long page_prefetch_preds(FeatureVector features[], int *response_arr);
unsigned long page_postfetch_preds(FeatureVector features[], int *response_arr, int batch_size);
```

`init_prefetcher()` is called once from `rmem_common_init()`.
`page_postfetch_preds()` is the one actually exercised today — called once
per candidate pointer with `batch_size == 1` from `page_postfetch()` in
`rmem/prefetch.c`, writing a 0/1 verdict into `response_arr[0]`.
`page_prefetch_preds()` is currently a dead stub (always returns 0) in the
source repo — the pointer-chase path never calls it.

To swap in the tree→C compiler's output: implement these three functions
(or thin wrappers around whatever the generated code's actual entry point
is called) in the new backend file, and drop the file into `rmem/` — since
it's build-dep-free, it needs no Makefile changes beyond being picked up by
the existing `rmem_src = $(wildcard rmem/*.c)` glob.

Two things to pin down once the generated code's interface is decided:
- **Feature set**: if it takes the same four inputs (`pc`, `offset`,
  `delta`, `offset_from_faulting`), `prefetch.h`'s `FeatureVector` and
  `prefetch.c`'s feature-construction loops in `page_postfetch()` port
  unchanged. If the generated predictor wants different/fewer/more inputs,
  both of those need editing too, not just the new backend file.
- **Statefulness/thread-safety**: fltrace's XGBoost backend keeps one
  global `booster` behind a mutex, initialized once. If the generated C
  tree code is just a pure function (no shared mutable state, e.g. a
  straight if/else cascade compiled from the tree), `init_prefetcher()` may
  end up doing nothing at all — simpler than what it's replacing.

## The two things that need real design work, not just copying

### 1. RDMA backend has no `post_read_prefetch` at all — ❌ still not done

fltrace never had an RDMA backend, so this function doesn't exist anywhere
to copy from. Eden's `common.c` picks `rdma_backend_ops` for
`RMEM_BACKEND_RDMA`, and once `post_read_prefetch` is added to the shared
`rmem_backend_ops` struct, `rdma_backend_ops` in `rmem/rmem_rdma.c` will
have it implicitly NULL — a crash waiting to happen the first time
`page_postfetch()` runs under the RDMA backend.

The harder issue is **synchrony**: fltrace's `page_postfetch()` calls
`rmbackend->post_read_prefetch(...)` and immediately calls
`prefetch_read_done()` on the same buffer, assuming the read already
completed — true for the LOCAL backend (`local_post_read_prefetch` does a
synchronous `memcpy`), but **false for RDMA**, where reads are posted async
and completed later via `check_for_completions()` polling (see
`rdma_post_read()` / `rdma_check_cq()` in `rmem/rmem_rdma.c`). Porting the
synchronous call pattern as-is against RDMA will read uninitialized/garbage
data out of the scratch buffer.

Recommended staging:
- **v1**: implement `rdma_post_read_prefetch()` as a *blocking* RDMA read
  (post the read, then spin on completion for that specific request before
  returning) — matches the LOCAL backend's synchronous contract with no
  changes needed in `prefetch.c`. Costs a stall on the calling handler
  thread per prefetch, acceptable for validating the model/feature pipeline
  end-to-end before optimizing.
- **v2** (only if throughput matters): make prefetch reads fully async —
  track pending prefetch requests distinct from fault-driven reads, and
  call `prefetch_read_done()` from the completion callback instead of
  inline in `page_postfetch()`. This is a real structural change to
  `prefetch.c`, not a port.

### 2. The `pc` feature needs a kernel patch (you're handling this directly) — ❌ still not done (kernel patch pending; `fault_t.pc` plumbing is in place behind the new `UFFD_PC_SUPPORTED` flag, which is currently undefined)

fltrace reads `message.arg.pagefault.pc` in `handler.c`'s
`read_uffd_fault()`. Checked directly: `/usr/include/linux/userfaultfd.h`
on this machine has no `pc`/`ip` field in `uffd_msg.pagefault` — only
`flags`, `address`, `feat.ptid`. fltrace's own `Makefile` diff adds
`-I/data1/linux/usr/include`, i.e. it points the build at a *patched* local
kernel header tree. This lines up with `kernel/README.md` in this repo,
which documents an optional `uffd-include-ip.patch` ("includes ip register
value in the fault message... not necessary for proper functioning of
Eden... only required for the older versions of the tracing tool") — the
same patch fltrace relies on.

Once the kernel is updated with this field, `handler.c`'s
`read_uffd_fault()` and `fault.h`'s `fault_t.pc` port directly per the
file-by-file plan above. No action needed here beyond that dependency
being satisfied before the `pc` feature will compile.

## `fault_t` layout math (verified against Eden's current header)

Eden's current `fault_t` (`inc/rmem/fault.h`) is exactly 64 bytes (1 cache
line): 8-byte flag/metadata header + `page`/`mr`/`thread`/`bkend_buf`/
`tstamp_tsc` (5×8) + `link` (16). Adding `faulting_addr` + `pc`
(`uint64_t` × 2 = 16 bytes) brings it to 80 bytes. fltrace pads its own
(coincidentally also-64-byte) base struct up to 128 bytes with a 48-byte
`cacheline_padding` field — the same `128 - 80 = 48` padding applies
cleanly here too. Keep the existing `BUILD_ASSERT(sizeof(fault_t) %
CACHE_LINE_SIZE == 0)` as the check.

## Build system

Much simpler than fltrace's own build, since the generated predictor has no
external dependencies:

- `rmem/*.c` is glob-included by the top-level `Makefile` (`rmem_src =
  $(wildcard rmem/*.c)`), so `prefetch.c` and the new predictor file are
  picked up automatically — no Makefile edit needed to compile or link
  them. None of fltrace's XGBoost-specific build machinery applies here
  (no `-lxgboost`, no `LD` switch to `g++`, no `dlopen(RTLD_GLOBAL)`
  preload trick, no `LD_PRELOAD` wrapper-script change) — all of that goes
  away with the predictor swap.
- The only remaining build-time decision is whether to gate the feature
  behind a flag at all (mirroring how `REMOTE_MEMORY` gates the RDMA/uffd
  path) — with no heavy dependency to justify it, this is now optional
  rather than necessary, but may still be worth it just to keep prefetch
  code out of builds that don't want it.
- `DO_PREFETCH` and `DO_RDAHEAD` in fltrace's `fault.c` are both plain
  `#define`s (commented out in the shipped diff — the feature is off by
  default even in the source repo). Decide explicitly whether read-ahead
  and content-directed prefetch should coexist or be mutually exclusive
  before wiring up the equivalent flag here.

## Rough edges in the source worth knowing before/while porting

- Debug `fprintf`/`printf` calls left in hot paths (`page_lock_acquire`,
  `rmem_common_destroy`, prefetch success paths) — strip before any
  performance testing.
- `local_post_read_prefetch()` has `assert(offset > 0)` where `offset == 0`
  (prefetching the very first byte of a region) would trip the assert;
  harmless in practice given page alignment but worth a second look.
- The "next-N sequential" candidate path allocates/initializes 88 feature
  slots (`features[512..600]`) in `prefetch_init()`/`page_postfetch()`, but
  only ever evaluates 4 of them (`i = 512..516`) — the rest are dead range,
  not a bug, just unused headroom.
- `is_page_prefetchable()` has the author's own `XXX` note questioning
  whether it needs the same extra checks as `fault_can_rdahead()` — worth
  resolving rather than carrying the TODO forward silently.
- Since `xgboost_prefetcher.c` isn't being ported, its share of the ~1200
  LOC (501 lines, of which roughly the last 100 were a commented-out
  `main()` test harness + comment-only notes) drops out of scope entirely
  — the actual ported surface is `prefetch.h`/`prefetch.c` plus the small
  patches listed above, well under half the original diff.

## Suggested staging order

1. ✅ **Done** Port into `tools/fltrace/` (`RMEM_STANDALONE` build via `make
   fltrace.so`) first — it already calls the same `rmem_common_init()`
   entry point, has no Shenango-stealing code to reconcile, and only needs
   the LOCAL backend's `post_read_prefetch`. This validates the model
   loading, feature pipeline, and page-fault-path integration with the
   least new work, closely mirroring the source repo.
2. ✅ **Done** Extend to the full Shenango-integrated runtime (`rmem/handler.c`'s
   `#ifndef RMEM_STANDALONE` paths) once step 1 works. (Landed in the same
   commit as step 1, not as a separate follow-up.)
3. ❌ **Not done** Implement `rdma_post_read_prefetch()` (blocking v1, per above) to run
   the same test across smarties06/smarties10 instead of the LOCAL
   backend — this is the real Eden deployment target this session set up
   in `network_setup.md`. **This is the blocker for running the prefetcher
   end-to-end over RDMA.**
4. ❌ **Not done** Land the kernel `pc` patch (in progress separately) and confirm
   `fault.h`/`handler.c`'s `pc` plumbing compiles and populates correctly.

## Verification

- Build check: `make clean && make REMOTE_MEMORY=1` (plus whatever flag is
  chosen, if any) should compile cleanly with the new files linked in —
  no XGBoost/libstdc++ link errors to worry about with the generated
  predictor.
- Functional check for step 1: run `tools/fltrace` against a
  pointer-chasing workload (e.g. a linked-list/graph traversal) with
  `DO_PREFETCH` on, and confirm `RSTAT(PREFETCHES)` increments and pages
  read via prefetch are later evictable/faultable normally.
- Functional check for step 3: reuse this session's `rcntrl`/`memserver`/
  `test_rmem_touch` setup (`network_setup.md`) with a workload that
  actually chases pointers within the allocated region (unlike
  `test_rmem_touch`'s flat buffer, which won't exercise the pointer-chase
  path at all), and confirm no correctness regressions in the existing
  fault/eviction path plus a measurable prefetch hit rate.
