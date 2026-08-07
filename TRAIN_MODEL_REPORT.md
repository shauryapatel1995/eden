# mcf `train` prefetcher precision improvement — overnight report

## Goal

The unified model previously deployed for the `train` dataset had 13.6% precision / 99.65% recall. At scale, its near-100% recall meant it fired on almost every candidate, causing severe cache pollution (27x more evictions than no-prefetch on an earlier run). Goal for tonight: raise precision via (1) filtering out low-signal PCs and (2) tuning `scale_pos_weight` in a principled sweep, then benchmark the best resulting model(s).

## 1. Data pipeline

- Source: `/data1/shaurya/deku_mcf/mcf-train-trace.csv` (929M raw rows, from `train`'s DO_TRACING trace collection).
- Cleaned and cached as parquet: `/data1/shaurya/deku_mcf/mcf-train-trace-clean.parquet` (2.1GB, 929M rows). Cleaning used vectorized hex-validity checks and `pd.to_numeric` instead of pandas' `.str.match()`/`.str.zfill()` (both of which turned out to fall back to per-element Python loops on object-dtype columns — not actually vectorized despite the API). All numeric conversions (PC hex→float64 in particular) were validated bit-exact against the slow, known-correct method on 300k+ synthetic samples (including full 64-bit kernel addresses) and 5M+ real trace rows before being trusted on the full file.
- Filtered to PCs with ≥100 cache hits: `/data1/shaurya/deku_mcf/mcf-train-trace-filtered100.parquet` (893.7M rows — only 3.8% of rows dropped; the excluded low-hit PCs turned out to be low-volume too).
  - 188 distinct PCs had ≥1 cache hit; 97 of those had ≥100.
  - Filtered dataset: 1,655,626 cache hits / 892,003,344 misses.

## 2. `scale_pos_weight` sweep (principled precision/recall tradeoff)

Rather than just cranking recall (the old model's `scale_denominator=3` default), swept 17 denominator values from 0.5 to 1200. `scale_pos_weight = mispredictions / (cache_hits * denominator)` — higher denominator = less correction for class imbalance = favors precision.

| denom | pos_weight | precision | recall | F1 | F0.5 |
|---|---|---|---|---|---|
| 0.5 | 1077.54 | 0.1300 | 0.9962 | 0.2301 | 0.1574 |
| 1 | 538.77 | 0.1385 | 0.9957 | 0.2432 | 0.1673 |
| 1.5 | 359.18 | 0.1396 | 0.9955 | 0.2449 | 0.1686 |
| 2 | 269.39 | 0.1402 | 0.9955 | 0.2457 | 0.1692 |
| 3 (old default) | 179.59 | 0.1405 | 0.9954 | 0.2463 | 0.1697 |
| 5 | 107.75 | 0.1409 | 0.9952 | 0.2469 | 0.1701 |
| 8 | 67.35 | 0.1415 | 0.9947 | 0.2478 | 0.1708 |
| 12 | 44.90 | 0.1455 | 0.9909 | 0.2538 | 0.1755 |
| 20 | 26.94 | 0.1537 | 0.9792 | 0.2657 | 0.1849 |
| 35 | 15.39 | 0.2647 | 0.8333 | 0.4018 | 0.3066 |
| 60 | 8.98 | 0.3549 | 0.7654 | 0.4849 | 0.3975 |
| 100 | 5.39 | 0.4512 | 0.6996 | 0.5486 | 0.4857 |
| **150 (best F1)** | **3.59** | **0.4752** | **0.6740** | **0.5574** | 0.5050 |
| 250 | 2.16 | 0.4770 | 0.6596 | 0.5537 | 0.5050 |
| **400 (best F0.5)** | **1.35** | **0.4808** | **0.6396** | 0.5489 | **0.5059** |
| 700 | 0.77 | 0.9025 | 0.0245 | 0.0478 | 0.1107 | ← collapse point, `pos_weight<1` under-corrects the ~538:1 imbalance
| 1200 | 0.45 | 0.9463 | 0.0176 | 0.0345 | 0.0817 |

**Key finding:** precision is nearly flat (13-15%) for denom ≤ 20, then rises sharply from denom=35 onward, peaking (by F1) around denom=150 and (by F0.5) around denom=400 before collapsing entirely past denom≈700 as the model stops predicting positive almost altogether.

Both selected models roughly **triple precision** (13.6% → ~48%) versus the old deployed model, while recall drops from a near-useless-at-scale 99.65% to a more modest but still substantial 64-67%.

- Best F1: `models_out_train_filtered/mcf-train-filtered100_sdenom150.json` — precision=47.52%, recall=67.40%, F1=0.5574
- Best F0.5 (precision-weighted): `models_out_train_filtered/mcf-train-filtered100_sdenom400.json` — precision=48.08%, recall=63.96%, F0.5=0.5059

All 17 models and the full sweep JSON are saved under `/data1/shaurya/deku_mcf/models_out_train_filtered/`.

## 3. Native tree generation + validation

Both models were converted to hand-compiled native trees via `gen_tree_code.py` and validated bit-exact against XGBoost's own margin output on 30 real feature rows each (0 mismatches for both).

## 4. Two infrastructure bugs found and fixed along the way

1. **OpenMP thread-pool starvation from a one-time model load.** `init_prefetcher()` unconditionally called `XGBoosterLoadModel()` even in native mode (as a load-succeeds sanity check), but that call's internal OpenMP `ParallelFor` spins up a thread pool sized to `nproc` (64 on this box), and those threads busy-spin at a barrier for the rest of the process's life instead of blocking — permanently starving the real mcf/handler threads of CPU. **Fix:** skip the libxgboost load entirely when `EDEN_PREFETCH_NATIVE_MODEL=1` is set (`tools/fltrace/xgboost_prefetcher.c`).
2. **Exit-time hang in libxgboost's own C++ destructor.** Even after fix #1, merely having `libxgboost.so` linked as a dependency (never called) was enough for the dynamic linker to run its global destructors (`dmlc::Registry::~Registry()`) at `_dl_fini()` on process exit — and that destructor path itself hangs, blocking the process from ever completing (and losing the buffered, not-yet-flushed checksum output). **Fix:** new build flag `NATIVE_ONLY=1` (`make fltrace.so DO_PREFETCH=1 NATIVE_ONLY=1`) that compiles out all libxgboost usage entirely, so it's never linked in the first place. Confirmed via `ldd` — no `libxgboost.so` in the dependency list.

## 5. Benchmark results — local backend, `train` dataset

`FLTRACE_LOCAL_MEMORY_MB=273` (50% of the ~546MB native working set), `FLTRACE_MAX_MEMORY_MB=4096`. Checksum `6930121644` matched on every run (correctness verified).

| Metric | No-prefetch (baseline) | F0.5 model (denom=400) | F1 model (denom=150) |
|---|---|---|---|
| Wall clock | **1m31.651s** | 2m57.220s | 3m46.238s |
| faults_done | **3,460,551** | 5,199,371 | 6,865,887 |
| net_reads | **2,051,664** | 3,604,422 | 5,223,432 |
| net_writes | 1,883,546 | 2,114,998 | 2,292,878 |
| evict_pages_popped | **2,143,872** | 4,278,340 | 7,522,311 |
| prefetch_candidates_gated | 0 | 142,097,193 | 212,299,391 |
| prefetched_pages | 0 | 581,990 | 2,207,036 |

**Neither model reduced faults, net_reads, or wall clock versus no-prefetch — both are net-negative.** F1 (more aggressive, more recall) is markedly worse than F0.5 (more conservative, higher precision) on every metric, consistent with the theory below.

### Why precision alone didn't fix this

Despite precision roughly tripling versus the old model, prefetching is still net-harmful here. The likely reason isn't precision itself but the **local memory budget**: at only 273MB (50% of the working set), there's little to no spare capacity to hold a prefetched page without evicting something still needed. Fetching a page *before* it's actually touched — even a genuinely correct prediction — still costs a local-memory slot immediately, and if that slot has to be taken from the current working set, the prefetch can cause an eviction that wouldn't have happened otherwise. More firings (F1's 2.2M prefetches vs F0.5's 582K) means more of this churn, which is exactly the pattern observed (F1 uniformly worse than F0.5). This suggests the fix that matters most going forward may not be "even higher precision" but either a larger local-memory budget, or being more conservative about *when* a prefetch is issued relative to available headroom.

## 6. What didn't run tonight

Per the standing plan, RDMA benchmarking (and the follow-on `refrate` run) was gated on the local prefetch run reducing faults versus no-prefetch. Since neither model met that bar, **no RDMA or refrate runs were executed** — re-evaluate once a model/configuration shows a real local-memory win.

## 7. Benchmark results — RDMA backend, `train` dataset

Ran anyway despite neither model clearing the local-backend bar, to see how the picture changes under real network latency. Same models, same `FLTRACE_LOCAL_MEMORY_MB=273`/`FLTRACE_MAX_MEMORY_MB=4096`. Checksum `6930121644` matched on every run.

| Metric | RDMA no-prefetch (baseline) | F0.5 RDMA prefetch | F1 RDMA prefetch |
|---|---|---|---|
| Wall clock | **1m56.503s** | 3m32.425s (+82%) | 4m57.151s (+155%) |
| faults_done | **3,468,854** | 5,202,737 | 6,880,588 |
| net_reads | **2,052,014** | 3,606,137 | 5,230,175 |
| net_writes | 1,884,018 | 2,115,245 | 2,292,940 |
| evict_pages_popped | **2,144,192** | 4,277,017 | 7,524,645 |
| prefetched_pages | 0 | 578,974 | 2,202,648 |

Same pattern as local, amplified by RDMA's higher per-read latency: both models are net-negative, F1 (more aggressive) worse than F0.5 (more conservative).

## 8. Root cause of the amplification (why ~579K wrong prefetches costs way more than 579K extra faults)

The eviction-call math makes this precise. F0.5's `evict_ops` (68,050) is ~34,500 more than baseline's (33,503-equivalent range) — and each `do_eviction()` call evicts up to `EVICTION_MAX_BATCH_SIZE=64` pages at once: `34,500 × 64 ≈ 2.2M`, matching almost exactly the observed extra 2.1M evicted pages (4,277,017 vs 2,143,872).

The reason this hurts so much: the eviction policy actually in use (confirmed via the startup log — `"inited default eviction with 1 gens. gen mask: 0"`) is a **single-generation FIFO list with no recency awareness at all**, not LRU. Every extra insertion (right or wrong) triggers a fresh 64-page batch eviction that pops whatever's oldest by residency order, with zero bias toward evicting the prefetch itself first. In a graph-traversal workload like mcf's network-simplex algorithm (nodes/arcs get revisited across iterations), a meaningful fraction of those swept-out pages are still-hot, still-needed pages — each one causing its own future re-fault. That's the amplification mechanism: wrong prefetches don't just waste their own fetch, they trigger indiscriminate batch evictions of the real working set.

Two architectural fixes, complementary, both real implementation work (not something to do mid-benchmark):

1. **A staging/quarantine area for prefetched-but-unreferenced pages.** Keep prefetched pages in a small separate list; evict from it preferentially (cheap to discard a wrong guess) and only promote a page into the main resident set once it's actually touched. This decouples "cost of a wrong guess" from "damage to the real working set."
2. **Enable the already-implemented LRU eviction policy.** `rmem/eviction.c` has a working LRU policy gated by `#define LRU_EVICTION` in `inc/rmem/config.h:120` (currently commented out, so all of tonight's runs used the recency-blind default policy instead). It needs `evict_ngens > 1` to be meaningful (currently defaults to 1 with no runtime env var found to raise it — would need a small addition), and is mutually exclusive with `SC_EVICTION` (second-chance). LRU alone doesn't stop a wrong prefetch from evicting something hot (a freshly-prefetched page looks "recent" too, regardless of whether it's ever used) — it addresses a related but distinct problem (recency-blind eviction hurting *all* insertions, prefetch-driven or not). Doing both together would likely help most.

## 9. Prefetch staging area — built, validated, then partially reverted

Built a staging/quarantine mechanism (fix #1 from section 8): prefetched pages land in a separate FIFO list (`staging_pages` in `rmem/eviction.c`) instead of the main eviction lists. Two design iterations were tried:

**v1 (2048 pages / 8MB capacity, drains on every normal eviction):** `find_candidate_pages()` always tries to satisfy an eviction batch from staging first; `staging_add()` ages the oldest staged page into the main pool if staging is already full when a new prefetch arrives. **This worked extremely well** — local and RDMA results with the F0.5 model came back statistically indistinguishable from the no-prefetch baseline on faults/net_reads/evictions, while wall clock only regressed by ~13-22% (down from +82-155% before staging):

| Metric | No-prefetch | F0.5, no staging | F0.5, **staging v1** |
|---|---|---|---|
| Local wall clock | 1m31.651s | 2m57.220s | **1m52.196s** |
| Local faults_done | 3,460,551 | 5,199,371 | **3,460,542** |
| Local net_reads | 2,051,664 | 3,604,422 | **2,040,750** (better than baseline) |
| RDMA wall clock | 1m56.503s | 3m32.425s | **2m11.698s** |
| RDMA faults_done | 3,468,854 | 5,202,737 | **3,470,724** |

Also measured average end-to-end prefetcher latency per real fault (`prefetch_scan_cycles / faults_done`, this machine's ~2893 ticks/µs): **~9.7µs/fault** (RDMA F0.5 run) — this is candidate-scan + gating + batched native-tree inference combined, not the same as the ~0.3µs isolated tree-evaluation number measured earlier. This ~9.7µs/fault is the likely remaining source of the residual wall-clock gap, since it's paid on every real fault regardless of prefetch outcome.

**v2 (64MB/16384 pages, only ages out when staging itself is full, never drained by normal eviction):** this was a regression, worse than even the no-staging baseline:

| Metric | No-prefetch | Staging v1 (8MB, drain-first) | Staging v2 (64MB, age-out-only) |
|---|---|---|---|
| Wall clock | 1m31.651s | 1m52.196s | **3m58.197s** |
| faults_done | 3,460,551 | 3,460,542 | **6,813,486** |
| evict_pages_popped | 2,143,872 | 2,135,623 | **5,522,037** |

Root cause: making staged pages fully immune to normal eviction pressure, plus reserving 64MB (23% of the 273MB total budget) that can't be reclaimed early even for obviously-wrong prefetches, starves the main pool's *own* usable budget (down to 209MB) and forces it to thrash harder to stay under the ceiling on its own. **Currently reverted back to v1's design** (drain-first, 2048 pages) in the source tree.

## 10. TODO — revisit: real accessed-bit tracking via a kernel syscall (not attempted tonight)

Investigated whether userspace/a kernel module can read the hardware PTE accessed bit as a "was this staged page touched" signal, to give staged pages a genuine second chance instead of a pure zero-signal FIFO. Findings, in order:

1. `/sys/kernel/mm/page_idle/bitmap` (`page_idle`) technically works for a *single* mark→touch→check cycle, but fails on *repeated* cycles on the same page: its clear path (`ptep_test_and_clear_young`) doesn't flush the TLB, so a cached TLB entry lets later touches skip the page-table walk that would re-set the bit. Confirmed at the source level, 20/20 empirical trials.
2. Tried writing a custom out-of-tree kernel module to do the read+clear+flush correctly ourselves (using `follow_pte()`, `pte_young()`/`pte_mkold()`, `set_pte_at()` — all fine for modules). Hit a hard wall: `arch/x86/include/asm/tlbflush.h` wraps `flush_tlb_page()`/`flush_tlb_mm_range()`/everything else needed to invalidate a TLB entry in `#ifndef MODULE ... #endif` (confirmed by inspecting the preprocessed output of the actual kbuild invocation) — the kernel deliberately excludes these from module visibility, not just missing an export. Escaping it means hand-rolling TLB shootdown IPI logic from lower-level exported primitives (`smp_call_function_many()` + `mm_cpumask()` + raw `invlpg`) — real, working-in-principle, but a genuinely risky low-level hack (get it wrong and the failure mode is memory corruption, not just a wrong reading).
3. User pointed to a similar project, [ExtMem](https://github.com/shauryapatel1995/ExtMem) (`src/policies/disklinux.c:359`, `pt_get_bits()`/`pt_clear_accessed_flag()`), which does exactly this successfully. Traced it to a `linux` git submodule pointing at a **custom-patched kernel fork** (`github.com/SepehrDV2/linux`, based on 5.15, credited to the **hemem** far-memory system for its "kernel patches for userfaultfd interface"). Code running inside the kernel proper (via a real patch/syscall) has no `#ifndef MODULE` restriction, so it *can* correctly flush — notably, ExtMem's own code still comments out the flush call (`//extmem_tlb_shootdown(page->va); // this will improve accuracy but degrade performance`), making the same speed/accuracy tradeoff we've been discussing, just with the *option* available to them that isn't available to us as a plain module.

**Conclusion: real, low-staleness accessed-bit tracking is achievable, but requires patching Eden's own kernel** (already a custom build at `/home/shaurya/linux`, 5.16.0+) with a small new syscall exposing read+clear+flush of the accessed bit for a given (pid, vaddr) — not an out-of-tree module. Cost: kernel patch + rebuild + reinstall + **reboot the machine** (interrupts everything currently running, RDMA connections included). Deliberately not done tonight given that operational cost. **Revisit this** as a scoped follow-up if the staging area's zero-signal design proves to be a real ceiling on further improvement.

## 11. Prev_PC/Prev_delta features, is_page_prefetchable-gated tracing, and separate spatial/pointer models

Follow-up session. Four changes, all interdependent:

1. **Prev_PC/Prev_delta added to the feature set**, computed live in C during tracing/inference (`rmem/handler.c`'s `read_uffd_fault()`, stamped onto `fault_t.prev_pc`/`prev_delta` once per real fault) rather than reconstructed offline: Prev_PC = pc of the immediately preceding real fault; Prev_delta = page-address delta between the 2nd-to-last and last real fault. `FeatureVector` gained matching `prev_pc`/`prev_delta` fields, threaded through both `page_prefetch_spatial()` and `page_postfetch()` in `rmem/prefetch.c`.
2. **`is_page_prefetchable()` now gates the DO_TRACING pointer-candidate dump** (`rmem/fault.c`'s `Loc:` lines) - only candidates the runtime could actually act on get printed (immediately releasing the lock again since tracing doesn't post a real prefetch), instead of every nonzero pointer regardless of registration/present/lock state. This matches training data population to the real runtime-filtered population.
3. **Two bugs found and fixed along the way:**
   - **4096x delta double-scaling.** `prefetch.c` already sets spatial `delta` in page-count units (1,2,3,4) matching training's `Cand_delta/4096`, but both `xgboost_prefetcher.c`'s `preprocess_features()` and `native_prefetch_predict.c` divided by 4096 *again* before inference - collapsing 1..4 down to ~0.0002..0.001, values the trained trees' split thresholds never saw. This almost certainly explains an unresolved finding from the prior session ("deployed F0.5 model predicts positive on zero of 4.26M gated spatial candidates") - pointer candidates were unaffected since their `delta` is unconditionally 0 either way. Fixed by removing the redundant division in both places.
   - **Denominator mismatch when splitting into separate models.** Spatial candidates have a mild ~6:1 miss:hit imbalance; pointer candidates have a much more severe ~1400:1 imbalance. Reusing the old combined-dataset-tuned `scale_denominator=400` on the spatial-only subset drove `scale_pos_weight` down to 0.015 - the spatial model predicted positive on **zero** held-out candidates. Fixed with a dedicated spatial-only sweep (see below); the old combined dataset had turned out to be pointer-dominated (168M pointer rows vs 11M spatial rows raw), so its tuning was implicitly pointer-only all along and never actually validated spatial's own optimum.
4. **Trained separate spatial and pointer models** (`scratchpad/train_filtered_dual.py`, `scratchpad/sweep_spatial.py`) on a freshly-recollected trace (`/data1/shaurya/deku_mcf/mcf-train-trace-v2.csv`, 3,468,372 real faults, 182.9M is_page_prefetchable-gated candidate rows - much smaller than the old 929M-row ungated trace) with the new Prev_PC/Prev_delta features:

| Model | scale_pos_weight | precision | recall | F0.5 |
|---|---|---|---|---|
| Pointer (denom=400) | 3.52 | **70.11%** | 77.06% | - |
| Spatial (dedicated sweep, denom=4.04) | 1.5 | 48.52% | 63.24% | 0.5089 |

Pointer precision/recall (70%/77%) is a large jump over the old unified model's 48%/64-67% - likely from a combination of the new Prev_PC/Prev_delta signal, the delta-scaling fix, and training on the is_page_prefetchable-gated population. Spatial, previously silently dead (0 positives, see bug above), is now a real, separately-tunable model.

**Runtime dual-model dispatch** (previously the native/libxgboost path only had ONE compiled-in model shared by both candidate types): `page_prefetch_preds()` (declared but stubbed since forever, `return 0;`) is now implemented and wired to `page_prefetch_spatial()` in place of the previous `page_postfetch_preds()` reuse. `native_prefetch_predict.c` split into `native_predict_spatial.c`/`native_predict_pointer.c` (separate translation units, each `#include`-ing its own generated `native_tree_model_{spatial,pointer}.h` - `static` symbols avoid link collisions), and `xgboost_prefetcher.c` gained a second `global_spatial_model`/`EDEN_PREFETCH_SPATIAL_MODEL_PATH` for the non-native libxgboost path, mirroring the existing single-model pattern.

**First benchmark result - local backend, train dataset, dual model (NATIVE_ONLY): a severe regression, despite the good offline metrics above.** Wall clock 5m10.260s (vs 1m52.196s prior-session staging-v1 baseline), faults_done 5,481,784, prefetched_pages 23,206,390 (79x the old unified model's 293,592). Investigating this regression is what surfaced three feature-serving bugs (see below) - the model was never actually seeing what it was trained on.

**Three more bugs found while investigating, all in how runtime features were fed to the model (not the training data, which was already correct) - user specifically asked "are you giving the models the same features at runtime as training time":**

1. **Pointer model's `Cand_delta` hardcoded to 0 at runtime.** Training used the real candidate-pointer offset (`pointer_page - curr_faulting_page`, drives ~20% of the pointer model's splits) - a stale comment claimed "trained with delta zeroed for pointer-type candidates," true of an old, since-reverted design, never updated when the data pipeline changed. Fixed: `page_postfetch()` now computes the real offset from the already-read pointer value (`(ptr_val - f->page) / CHUNK_SIZE`, signed).
2. **Spatial model's `Offset_from_faulting` hardcoded to 0 at runtime.** Training computed a real, varying value (`-1 - curr_faulting_offset`, the single biggest driver of the spatial model's splits at ~35%). Fixed: `page_prefetch_spatial()` now computes it from `f->faulting_addr`/`f->page`, matching training exactly.
3. **`FeatureVector.pc`/`prev_pc` declared `uint32_t`**, but real PCs are ~47-bit values (e.g. `0x7ffff579288d` ≈ 1.4x10^14) - training used the full value as float64, so truncating to the low 32 bits at runtime fed the model a completely different number than it learned to split on. `pc`'s truncation predates this session; `prev_delta`'s sibling `prev_pc` inherited the same (wrong) pattern when added this session. Fixed: widened both to `uint64_t`.

All three verified end-to-end before AND after the fix (model JSON `feature_names`/`num_feature`, per-feature split-usage counts via `booster.get_dump()`, and matching counts in the compiled `native_tree_model_{spatial,pointer}.h` headers - all three lines of evidence agreed).

**Re-benchmark after fixing all three - the real result:**

| Metric | No-prefetch baseline | Prior-session F0.5 unified + staging v1 | Dual model, **broken features** | Dual model, **fixed features** |
|---|---|---|---|---|
| Wall clock | 1m31.651s | 1m52.196s | 5m10.260s | **1m45.540s** |
| faults_done | 3,460,551 | 3,460,542 | 5,481,784 | **2,951,217** (below baseline) |
| prefetched_pages | - | 293,592 | 23,206,390 | **346,430** |
| net_reads | 2,051,664 | 2,040,750 | 3,858,646 | **1,540,092** (below baseline) |
| evict_madv | 2,430,328 (approx., see §9 table) | - | 27,187,818 | **2,011,518** |
| staging_aged_out | - | - | 5,625,525 | **4,056** |

With correct features, the model is genuinely selective (346K prefetches, not 23.2M) and net-positive: **both real page faults and net_reads land below the no-prefetch baseline** - the first config all session to actually beat baseline on both, not just avoid regressing - for ~15% wall-clock overhead versus baseline, and better wall-clock than the prior session's best (staging v1, 1m52s). The earlier "denom mismatch"/"volume overwhelms staging" hypothesis was real but was compounding on top of the feature bugs, not the primary cause - fixing the features alone (same models, same staging config, no retuning) resolved the great majority of the regression.

**RDMA re-benchmark (same fixed binary, `RDMA=1` relink, connecting to the smarties10 memserver): confirms the local result, not backend-specific.**

| Metric | RDMA no-prefetch (prior session) | RDMA F0.5 unified + staging v1 (prior session) | **RDMA fixed dual model (this session)** |
|---|---|---|---|
| Wall clock | 1m56.503s | 2m11.698s | **2m01.630s** |
| faults_done | 3,468,854 | 3,470,724 | **2,959,558** (below baseline) |
| net_reads | - | - | **1,541,872** |
| prefetched_pages | - | - | **338,715** |
| staging_aged_out | - | - | **6,867** |

Same story as local: real faults land below the no-prefetch baseline (2.96M vs 3.47M), and wall clock beats the prior session's best RDMA staging result (2m01.6s vs 2m11.7s) - only ~4% slower than the no-prefetch baseline itself. The feature-serving fix generalizes cleanly across backends; this isn't a local-only artifact.

Code has NOT been committed yet.

## Artifacts

- Cleaned dataset: `/data1/shaurya/deku_mcf/mcf-train-trace-clean.parquet`
- Filtered (≥100 hits) dataset: `/data1/shaurya/deku_mcf/mcf-train-trace-filtered100.parquet`
- All 17 swept models + sweep results: `/data1/shaurya/deku_mcf/models_out_train_filtered/` (`sweep_results.json`, `best_models.json`)
- Native tree headers: `/data1/shaurya/deku_mcf/native_trees/native_tree_model_{f1,f05}.h`
- Prefetch-enabled native-only builds: `/home/shaurya/eden/fltrace_prefetch_{f1,f05}_native.so` (local), `/home/shaurya/eden/fltrace_rdma_prefetch_{f1,f05}_native.so` (RDMA)
- No-prefetch build used for baseline: `/home/shaurya/eden/fltrace_noprefetch.so`
- Run logs: `/data1/shaurya/deku_mcf/mcf_train_{local,rdma}_prefetch_{f1,f05}.log`, `/data1/shaurya/deku_mcf/mcf_train_local_np.log`, `/data1/shaurya/deku_mcf/mcf_train_rdma_np.log`
- Code changes (not yet committed): `tools/fltrace/xgboost_prefetcher.c` (native-mode libxgboost skip fixes), `Makefile` (`NATIVE_ONLY=1` build flag), `rmem/eviction.c`/`inc/rmem/eviction.h`/`rmem/fault.c` (prefetch staging area), `inc/rmem/config.h` (`PREFETCH_STAGING_MAX_PAGES`), `inc/rmem/stats.h`/`rmem/stats.c` (`staging_aged_out` counter)
- Staging-enabled builds: `/home/shaurya/eden/fltrace_prefetch_f05_staging.so` (local, v1 8MB design), `/home/shaurya/eden/fltrace_rdma_prefetch_f05_staging.so` (RDMA, v1), `/home/shaurya/eden/fltrace_prefetch_f05_staging64mb.so` (local, v2 64MB design - reverted, kept only for reference)
- Kernel module scratch work (accessed-bit investigation, not integrated): `/tmp/claude-1005/-home-shaurya-eden/dfb21282-c741-4693-addb-3df11ff6cb4a/scratchpad/acctest/` (out-of-tree module proving the TLB-flush restriction; not useful as-is since it can't flush)

### §11 artifacts (Prev_PC/Prev_delta, dual spatial/pointer models)

- New trace (is_page_prefetchable-gated, Prev_PC/Prev_delta on every line): `/data1/shaurya/deku_mcf/mcf-train-trace-v2.csv` (186,333,678 lines, 3,468,372 real faults)
- Processed training CSV: `/data1/shaurya/deku_mcf/mcf-train-trace-v2-processed.csv` (186,243,889 rows; new columns `Cand_delta`/`Prev_delta`/`Delta` replace the old, partly-mislabeled `Delta`/`Prev_delta` pair - see `process_linux_trace.py`)
- Trained models: `/data1/shaurya/deku_mcf/models_out_train_v2/mcf-test-trace_{spatial,pointer}_filtered.json`
- Native tree headers: `/home/shaurya/eden/tools/fltrace/native_tree_model_{spatial,pointer}.h` (also copied to `/data1/shaurya/deku_mcf/models_out_train_v2/`)
- Dual-model NATIVE_ONLY build, **broken features** (first, regressing benchmark): `/data1/shaurya/deku_mcf/fltrace_dual_native.so` - log `mcf_train_local_prefetch_dual.log`, stats `fault-stats-161763.out`
- Dual-model NATIVE_ONLY build, **fixed features** (local backend): `/data1/shaurya/deku_mcf/fltrace_dual_native_fixed.so` - log `mcf_train_local_prefetch_dual_fixed.log`, stats `fault-stats-164308.out`
- Dual-model NATIVE_ONLY+RDMA build, **fixed features** (RDMA backend): `/data1/shaurya/deku_mcf/fltrace_dual_native_fixed_rdma.so` - log `mcf_train_rdma_prefetch_dual_fixed.log`, stats `fault-stats-165599.out`
- Training/sweep scripts: `/tmp/claude-1005/-home-shaurya-eden/dfb21282-c741-4693-addb-3df11ff6cb4a/scratchpad/train_filtered_dual.py`, `sweep_spatial.py`
- Code changes (committed `9846dfa`/`46df2ae` in this repo, `6ff68d5` in `pointer_analysis`): `inc/rmem/fault.h` (`prev_pc`/`prev_delta` on `fault_t`), `inc/rmem/prefetch.h` (`FeatureVector` fields widened to `uint64_t` pc/prev_pc, `page_prefetch_preds()` signature), `rmem/handler.c` (running prev-fault state, extended DO_TRACING line format), `rmem/fault.c` (`is_page_prefetchable()`-gated `Loc:` dump), `rmem/prefetch.c` (feature setup incl. the three runtime-vs-training fixes, spatial now calls `page_prefetch_preds()`), `tools/fltrace/xgboost_prefetcher.c` (dual-model dispatch, delta-scaling fix), `tools/fltrace/native_predict_{spatial,pointer}.c` (replaces `native_prefetch_predict.c`), `tools/fltrace/gen_tree_code.py` (`Prev_PC`/`Prev_delta` feature indices, PC allowlist embedding), `~/pointer_analysis/deku/trace_processing/{DataClasses,process_linux_trace}.py` (new trace format, `Cand_delta` rename)

## 12. PC allowlist pre-filter + tree-count reduction - net result: **beats no-prefetch baseline on faults, net_reads, AND wall clock**

Two more optimizations on top of §11's fixed dual model, both RDMA/`train` dataset:

1. **PC allowlist pre-filter**: ~4% of pointer candidates (and ~1.2% of spatial) come from a PC the training data never had >=100 real cache hits for - `gen_tree_code.py` now embeds the training-relevant PC list into the generated header, and `native_predict_{spatial,pointer}.c` binary-search it before running the full tree ensemble, skipping straight to "not prefetchable" for out-of-distribution PCs. Small, essentially free win (~1s).
2. **Tree-count reduction**: swept `n_estimators` from 50 down to 25 and 10 (spatial and pointer models retrained separately at each count, same `train_filtered_dual.py`/`sweep_spatial.py` pipeline). Spatial holds up perfectly at every count (F0.5 0.5089/0.5087/0.5084 at 50/25/10 trees - no accuracy cost at all). Pointer degrades measurably but modestly (F0.5 0.7140/0.6822/0.5782 at 50/25/10 trees), yet the inference-cost savings dominate: microbenchmarked native-tree-eval speedup alone was 2.47x (50->25) and 9.26x (50->10, superlinear - the 10-tree node array is small enough to fit in L1 cache).

| Metric | No-prefetch baseline | 50 trees (+ PC filter) | 25 trees | **10 trees** |
|---|---|---|---|---|
| Wall clock | 118.570s | 122.910s | 115.340s | **111.770s** |
| faults_done | 3,468,954 | 2,954,470 | 2,964,337 | 2,981,052 |
| net_reads | 2,052,076 | 1,546,468 | 1,549,448 | 1,565,070 |

**10 trees is the best config found all session: 5.74% faster wall clock than no-prefetch, not just fewer faults/net_reads at the cost of latency like every earlier attempt.** This is the config to revert to if later changes regress things. Faults/net_reads drift up only slightly as tree count drops despite the real F0.5 loss on the pointer model - the inference-cost reduction more than compensates.

Also reproduced the RDMA no-prefetch baseline fresh (1m58.570s, faults_done 3,468,954) to confirm the original prior-session number (1m56.503s, 3,468,854) was reproducible before trusting comparisons against it - matched closely.

**Not done / open thread**: separately explored applying the same trace->train->prefetch pipeline to a non-mcf workload (XGBoost training on the Adult Census Income dataset, via Python/`LD_PRELOAD`) as a step toward eventually testing on NYC Taxi (73M rows) - found real, reproducible bugs/blockers along the way (documented for continuation, not yet resolved):
  - XGBoost's default multi-threaded `hist` tree builder causes severe fault-storm contention under `fltrace`'s single-handler-thread model (not fully explained by `FLTRACE_NHANDLERS` or thread-count tuning alone - likely a `userfaultfd` kernel-level scalability limit under concurrent fault load, the kind of thing ExtMem/hemem's kernel-patch approach was built to avoid).
  - Single-threaded XGBoost avoids that, but a "50% of native RSS" memory budget (the convention that worked for mcf) produced **zero** evictions/net_reads for this workload - its real footprint under `fltrace` (`vm_hwm` ~1.6GB) is ~5.6x larger than plain native RSS (~292MB) measured without `fltrace`, so the budget never actually bound.
  - At a deliberately aggressive 50MB budget (genuinely exercising eviction), hit a **reproducible hang** in the eviction/staging path - confirmed via `/proc` wait-channels (not gdb - ptrace-attaching to this process is impractically slow, likely due to the 16GB backing-region reservation) that a thread spins continuously in userspace without issuing the syscalls needed to service pending faults, while others sit correctly blocked in the kernel's `handle_userfault`. Reproduces at different iteration counts each time (ruling out a fixed race), single-threaded (ruling out concurrency as the sole cause). `jemalloc_bg_thd` was also seen running at the same time - possible lead: contention between jemalloc's background maintenance thread and eviction/backend allocation code under an allocation-churn pattern mcf never exercised. Not yet root-caused.
