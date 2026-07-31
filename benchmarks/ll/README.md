# LL (linked-list) prefetcher benchmark

Pointer-chasing microbenchmark used to test Eden's content-directed
prefetcher end-to-end via `fltrace.so`. Source:
`github.com/sid-agrawal/cheribsd`, `cp_benchmarks/pointers/linkedlist.c`
(branch `cherry-picking-merge`), ported to Linux (dropped the FreeBSD-only
`<sys/sysctl.h>` include and the per-node debug `printf`s). See
`../../PREFETCHER_PORTING.md` for the full prefetcher port history and the
bugs fixed to get this running.

## One-time machine setup

```bash
# allow unprivileged userfaultfd (needed for fltrace.so's uffd registration)
sudo sysctl -w vm.unprivileged_userfaultfd=1

# vendor a CPU-only libxgboost.so (skip if tools/fltrace/xgboost_cpu_lib/
# already exists - it's gitignored, not committed, since it's a large binary
# and would collide with whatever xgboost/xgboost-cpu you have pip-installed
# for other uses). The regular GPU-enabled `pip install xgboost` wheel
# deadlocks fltrace.so's handler thread on a GPU-less machine - see
# PREFETCHER_PORTING.md's "Done - LOCAL backend end-to-end test" section.
pip download --no-deps -d /tmp/xgboost_cpu_check xgboost-cpu
cd /tmp/xgboost_cpu_check && unzip -o xgboost_cpu-*.whl -d extracted
mkdir -p /home/shaurya/eden/tools/fltrace/xgboost_cpu_lib
cp extracted/xgboost/lib/libxgboost.so /home/shaurya/eden/tools/fltrace/xgboost_cpu_lib/
cp extracted/xgboost_cpu.libs/*.so* /home/shaurya/eden/tools/fltrace/xgboost_cpu_lib/
patchelf --set-rpath '$ORIGIN' /home/shaurya/eden/tools/fltrace/xgboost_cpu_lib/libxgboost.so
# verify: `ldd .../libxgboost.so` should show no "not found" deps, and
# `nm -D .../libxgboost.so | grep -i AllVisibleGPUs` should find nothing.

# if tools/fltrace/xgboost_include/xgboost/c_api.h is missing, fetch the
# header matching the vendored lib's version (check with
# `strings libxgboost.so | grep -m1 -A2 '^[0-9]\+\.[0-9]\+\.[0-9]\+$'` or the
# "xgboost version: x.y.z" log line from a run):
curl -sf -o /home/shaurya/eden/tools/fltrace/xgboost_include/xgboost/c_api.h \
  "https://raw.githubusercontent.com/dmlc/xgboost/v<VERSION>/include/xgboost/c_api.h"
```

## Build

```bash
cd /home/shaurya/eden
make fltrace.so DO_PREFETCH=1     # with the prefetcher (needs the xgboost setup above)
# make fltrace.so                 # baseline, no prefetching, no xgboost dependency

cd benchmarks/ll
make                               # builds ./ll
```

## Run

`ll` args: `<log2 num_nodes> <cyclesPerNode> <randomize traversal 0|1>`.
Each node is one 4096-byte page, so `log2 num_nodes = 12` => 4096 nodes =
16MB working set.

```bash
cd benchmarks/ll
rm -f fault-stats-*.out fault-samples-*.out procmaps-*

LD_PRELOAD=/home/shaurya/eden/fltrace.so \
FLTRACE_LOCAL_MEMORY_MB=4 \
FLTRACE_MAX_MEMORY_MB=64 \
EDEN_PREFETCH_MODEL_PATH=/home/shaurya/eden/models/random-ll-fltrace_xgboost_model.json \
EDEN_PREFETCH_XGBOOST_LIB_PATH=/home/shaurya/eden/tools/fltrace/xgboost_cpu_lib/libxgboost.so \
OMP_NUM_THREADS=1 \
./ll 12 10 1
```

Env vars, all required (fltrace.so has no sane defaults for the first two):
- `FLTRACE_LOCAL_MEMORY_MB` - local ("cached") memory size. Set below the
  working set to force real eviction/fault activity.
- `FLTRACE_MAX_MEMORY_MB` - total emulated remote memory pool. Defaults to
  **1MB** if unset - must be bigger than the app's working set or `rmalloc()`
  fails fast with "out of remote memory for alloc".
- `EDEN_PREFETCH_MODEL_PATH` - only used if built with `DO_PREFETCH=1`.
  `models/random-ll-fltrace_xgboost_model.json` was trained for this exact
  workload; `models/mcf-fltrace_xgboost_model.json` is for a different one.
- `EDEN_PREFETCH_XGBOOST_LIB_PATH` - only used if built with `DO_PREFETCH=1`;
  points the runtime `dlopen()` at the vendored CPU-only lib.
- `OMP_NUM_THREADS=1` - only relevant if built with `DO_PREFETCH=1`. Without
  this, XGBoost's OpenMP thread pool defaults to one thread per core
  (spins up ~a full core count's worth of threads) on every single
  `batch_size=1` prediction call - `page_postfetch()` makes hundreds of
  these per fault, so this is a large, avoidable slowdown, not just wasted
  threads.

## Checking results

Stats are written every second to `fault-stats-<pid>.out` (comma-separated
`name:value` pairs). Key fields:
- `faults` vs `faults_done` - should match at the end of a clean run; if
  `faults_done` lags and never catches up, something is stuck (see
  PREFETCHER_PORTING.md's bug list for known causes and fixes already
  applied).
- `prefetched_pages` - increments each time `page_postfetch()` actually
  prefetches a page. In the LOCAL-backend / `pc`-disabled test on
  2026-07-31, this was 0 for the whole run (see below).

## Known limitation: `pc` feature disabled

As of 2026-07-31, `inc/rmem/config.h`'s `UFFD_PC_SUPPORTED` is commented
out (no kernel patch applied), so `fault_t.pc` is always `0` for every
candidate. The `random-ll` model was very likely trained with real `pc`
values, so it's plausible it never predicts positive with `pc` pinned to 0 -
`prefetched_pages` was 0 across a full run, which is consistent with (but
not proven to be caused by) this. **Not yet root-caused** - could also be a
feature-construction/scaling bug independent of `pc`; nobody has added
per-prediction debug logging to check the raw model scores.

Once the kernel patch for `message.arg.pagefault.pc` is applied (**note**:
per the comment in `config.h`, this is *not* the same as the already-present
`kernel/uffd-include-ip.patch` in this repo - confirm which patch is
actually needed/applied before assuming `uffd-include-ip.patch` covers it):
1. Uncomment `#define UFFD_PC_SUPPORTED` in `inc/rmem/config.h`.
2. Rebuild (`make clean && make fltrace.so DO_PREFETCH=1`).
3. Re-run the same benchmark and check whether `prefetched_pages` becomes
   nonzero. If it's still 0, the model/feature pipeline needs the deeper
   debugging described above, not just the kernel patch.
