# Linux 6.6 swap prefetcher: all workloads

One run at a time over SSD swap (/data1/swapfile_swappf), THP never, caches dropped before and after every run, app killed after each run, cgroup v2 memory.max = the eden local memory.

Columns: **metric** = end-to-end metric (lower or higher is better, see the header), **majflt** = major page faults of the workload's cgroup in the measured phase, **prefetched** = pages read ahead (vmstat swap_ra; kernel read-ahead or the module), **used** = of those, pages later hit in the swap cache (swap_ra_hit). **vs default / vs noprefetch** = improvement of the config's median over the baseline's median, positive = better (time: base/x - 1; throughput: x/base - 1); in results_all.csv the same per run against the baseline median. Medians because some workloads (Redis) have an occasional slow-mode run in every config. Invalid runs are excluded.

Not run: omnetpp (104M swap-ins at its eden size, 111MB; about an hour per run on the SSD - skipped at the user's request; trace and trees are in /data1/shaurya/deku_omnetpp/linux66b). ll, adult, go and leela had no complete eden benchmark.

Rounds: 5 per config, except webservice and dataframe (1 run each, at the user's request - about 22 min per webservice run). Single-run medians are just that run.

SPEC: `mcf`, `cactu` and `xalan` are trained and tested on the same input (mcf/cactu train, xalan refrate) and are kept only for reference. The real SPEC results are `mcf_ref`, `cactu_ref` and `xalan_ref`: refrate input, trees trained only on the train-input trace (mcf/cactu trees from the train trace; xalan trees from `xalan_train`).

Configs: `noprefetch` = no prefetching (faulting page only); `default` = Linux default read-ahead (page-cluster 3); `n4_c1` = trees, N=4, claim rule on; `n4_c0` = trees, N=4, claim rule off; `n32_c1` = trees, N=32, claim rule on; `n32_c0` = trees, N=32, claim rule off; `leapc` = Leap (ATC'20) in the same module (mode 3): majority-trend detection over the last 32 faults, released-code window (hits+2, min 4, max 8 pages), aligned-block fallback without a trend; `st_ext4` = stack + spatial trees (Redis LRANGE stack test)

Leap rows (`leapc`) were run on the current kernel build (Nov 25-26, 2026); the other configs come from the October campaign (earlier builds of the same kernel). Leap's paper window (`leap`, shrinks to 0-1 pages without hits) was 28-50% slower than read-ahead and is not reported.

## Summary: medians, Deku (best tree config) vs Leap and read-ahead

| workload | metric | read-ahead | Leap | Deku (config) | Deku vs Leap | Deku vs read-ahead | Leap vs read-ahead |
|---|---|---|---|---|---|---|---|
| cactu | wall_secs (lower) | 60.26 | 59.17 | 50.02 (n32_c0) | 1.183x | 1.205x | 1.018x |
| cactu_ref | wall_secs (lower) | 495.11 | 508.25 | 478.94 (n32_c0) | 1.061x | 1.034x | 0.974x |
| dataframe | wall_secs (lower) | 413.84* | 415.25 | 296.92* (n32_c0) | 1.399x | 1.394x | 0.997x |
| etc | ops_per_sec (higher) | 11,337.21 | 8,403.73 | 11,387.79 (n4_c0) | 1.355x | 1.004x | 0.741x |
| gap_pr | avg_trial_secs (lower) | 52.87 | 50.22 | 34.32 (n32_c0) | 1.463x | 1.540x | 1.053x |
| gap_sssp | avg_trial_secs (lower) | 26.85 | 25.82 | 25.75 (n32_c0) | 1.003x | 1.043x | 1.040x |
| lbm_ref | wall_secs (lower) | 3,489.89 | 2,454.77 | 2,519.01 (n32_c0) | 0.974x | 1.385x | 1.422x |
| mcf | wall_secs (lower) | 82.77 | 79.88 | 78.58 (n32_c0) | 1.017x | 1.053x | 1.036x |
| mcf_ref | wall_secs (lower) | 1,183.03 | 1,065.08 | 908.15 (n32_c0) | 1.173x | 1.303x | 1.111x |
| memcached | kreq_per_sec (higher) | 693.10 | 695.70 | 695.20 (n32_c0) | 0.999x | 1.003x | 1.004x |
| memtier | ops_per_sec (higher) | 173,943.96 | 175,142.16 | 177,172.61 (n4_c0) | 1.012x | 1.019x | 1.007x |
| redis | lrange_rps (higher) | 9,653.44 | 9,860.96 | 10,595.47 (n4_c0) | 1.074x | 1.098x | 1.021x |
| redis_stack | lrange_secs (lower) | 904.85 | 734.59 | 514.90 (st_ext4) | 1.427x | 1.757x | 1.232x |
| webservice | wall_secs (lower) | 1,357.82* | 1,213.57 | 996.06* (n4_c0) | 1.218x | 1.363x | 1.119x |
| wiredtiger | wall_secs (lower) | 219.42 | 196.32 | 190.87 (n32_c0) | 1.029x | 1.150x | 1.118x |
| wt_c | wall_secs (lower) | 156.46 | 130.18 | 132.09 (n32_c0) | 0.986x | 1.184x | 1.202x |
| xalan | wall_secs (lower) | 268.18* | 258.51 | 261.10* (n4_c0) | 0.990x | 1.027x | 1.037x |
| xalan_ref | wall_secs (lower) | 248.63 | 257.38 | 248.01 (n4_c0) | 1.038x | 1.003x | 0.966x |

x = speedup (time: base/x; throughput: x/base); > 1 means the first-named config is better. \* = single run. `cactu`, `mcf`, `xalan` test on their training input (reference only).

## cactu

> SPEC 507.cactuBSSN_r, train input, 53MB (as eden). Trace = same input. Valid = specdiff vs SPEC expected.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh cactu noprefetch 1: cgroup /sys/fs/cgroup/swappf_cactu memory.max=55574528; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh cactu default 1: cgroup /sys/fs/cgroup/swappf_cactu memory.max=55574528; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh cactu leapc 1: cgroup /sys/fs/cgroup/swappf_cactu memory.max=55574528; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh cactu n4_c1 1: cgroup /sys/fs/cgroup/swappf_cactu memory.max=55574528; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh cactu n4_c0 1: cgroup /sys/fs/cgroup/swappf_cactu memory.max=55574528; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh cactu n32_c1 1: cgroup /sys/fs/cgroup/swappf_cactu memory.max=55574528; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh cactu n32_c0 1: cgroup /sys/fs/cgroup/swappf_cactu memory.max=55574528; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_cactu/linux66b/runs/noprefetch_r1) /data1/shaurya/spec2017/cactu_build/cactusBSSN_r spec_train.par > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 5 (5) | 94.39 | 94.73 ± 1.32 | 93.24453 / 94.38944 / 95.95115 / 93.83277 / 96.25644 | -36.16% | +0.00% | 2,641,739 | 0 | 47 | - |
| default | 5 (5) | 60.26 | 60.27 ± 0.19 | 60.49544 / 60.01701 / 60.25843 / 60.16971 / 60.39701 | +0.00% | +56.64% | 755,787 | 2,036,361 | 1,972,280 | 96.9 |
| leapc | 5 (5) | 59.17 | 59.18 ± 0.29 | 59.16947 / 58.83720 / 59.21666 / 59.06750 / 59.62730 | +1.84% | +59.52% | 532,329 | 2,271,484 | 2,208,978 | 97.2 |
| n4_c1 | 5 (5) | 72.76 | 72.55 ± 0.75 | 73.28546 / 72.76162 / 71.31518 / 72.89419 / 72.48283 | -17.18% | +29.72% | 1,334,350 | 1,344,896 | 1,327,689 | 98.7 |
| n4_c0 | 5 (5) | 56.99 | 56.95 ± 0.32 | 57.21025 / 56.99455 / 56.84515 / 57.23699 / 56.45942 | +5.73% | +65.61% | 626,434 | 2,051,328 | 2,044,627 | 99.7 |
| n32_c1 | 5 (5) | 79.33 | 79.28 ± 0.35 | 79.65512 / 79.50925 / 79.32711 / 78.76836 / 79.13910 | -24.04% | +18.99% | 1,622,787 | 1,204,304 | 1,129,751 | 93.8 |
| n32_c0 | 5 (5) | 50.02 | 49.99 ± 0.44 | 49.75673 / 50.45475 / 49.36702 / 50.34076 / 50.02384 | +20.46% | +88.69% | 211,264 | 2,688,614 | 2,585,292 | 96.2 |

## cactu_ref

> SPEC 507.cactuBSSN_r, refrate input (spec_ref.par), 395MB = 50% of native peak (790MB).
> Trees = the cactu (train-input) trees, symlinked from /data1/shaurya/deku_cactu/linux66b.
> Valid = specdiff vs SPEC refrate expected.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh cactu_ref noprefetch 1: cgroup /sys/fs/cgroup/swappf_cactu_ref memory.max=414187520; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu_ref/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh cactu_ref default 1: cgroup /sys/fs/cgroup/swappf_cactu_ref memory.max=414187520; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu_ref/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh cactu_ref leapc 1: cgroup /sys/fs/cgroup/swappf_cactu_ref memory.max=414187520; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh cactu_ref n4_c0 1: cgroup /sys/fs/cgroup/swappf_cactu_ref memory.max=414187520; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu_ref/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh cactu_ref n32_c0 1: cgroup /sys/fs/cgroup/swappf_cactu_ref memory.max=414187520; vm.page-cluster=3; insmod /data1/shaurya/deku_cactu_ref/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_cactu_ref/linux66b/runs/noprefetch_r1) /data1/shaurya/spec2017/cactu_build/cactusBSSN_r spec_ref.par > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 795.69 | 794.53 ± 2.27 | 795.6899 / 791.9078 / 795.9826 | -37.78% | +0.00% | 21,906,788 | 3 | 36 | 1090.0 |
| default | 3 (3) | 495.11 | 495.60 ± 0.88 | 495.1098 / 495.0668 / 496.6102 | +0.00% | +60.71% | 5,414,378 | 17,458,071 | 16,926,570 | 97.0 |
| leapc | 5 (5) | 508.25 | 507.56 ± 1.59 | 504.9050 / 508.6931 / 507.3012 / 508.6511 / 508.2502 | -2.59% | +56.55% | 4,492,085 | 17,601,276 | 17,488,445 | 99.4 |
| n4_c0 | 3 (3) | 529.39 | 530.22 ± 2.17 | 528.5879 / 529.3871 / 532.6876 | -6.47% | +50.30% | 7,670,084 | 14,241,754 | 14,230,859 | 99.9 |
| n32_c0 | 3 (3) | 478.94 | 478.65 ± 1.49 | 478.9447 / 477.0315 / 479.9731 | +3.38% | +66.13% | 4,924,620 | 17,238,857 | 17,160,732 | 99.5 |

## dataframe

> Dataframe (NYC taxi), 16.6GB test CSV, 15872MB (as eden). Trace = held-out month
> train_2016-07.csv at 1437MB (as eden). Valid = results == native reference.
> The app reads csv/all.csv and writes out/*.csv at absolute paths.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh dataframe noprefetch 1: cgroup /sys/fs/cgroup/swappf_dataframe memory.max=16642998272; vm.page-cluster=3; insmod /data1/shaurya/deku_dataframe/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh dataframe default 1: cgroup /sys/fs/cgroup/swappf_dataframe memory.max=16642998272; vm.page-cluster=3; insmod /data1/shaurya/deku_dataframe/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh dataframe leapc 1: cgroup /sys/fs/cgroup/swappf_dataframe memory.max=16642998272; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh dataframe n4_c1 1: cgroup /sys/fs/cgroup/swappf_dataframe memory.max=16642998272; vm.page-cluster=3; insmod /data1/shaurya/deku_dataframe/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh dataframe n4_c0 1: cgroup /sys/fs/cgroup/swappf_dataframe memory.max=16642998272; vm.page-cluster=3; insmod /data1/shaurya/deku_dataframe/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh dataframe n32_c1 1: cgroup /sys/fs/cgroup/swappf_dataframe memory.max=16642998272; vm.page-cluster=3; insmod /data1/shaurya/deku_dataframe/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh dataframe n32_c0 1: cgroup /sys/fs/cgroup/swappf_dataframe memory.max=16642998272; vm.page-cluster=3; insmod /data1/shaurya/deku_dataframe/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_dataframe/linux66b/runs/noprefetch_r1) /data1/shaurya/deku_dataframe/app/build/bin/main > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 1 (1) | 561.21 | 561.21 ± 0.00 | 561.2100 | -26.26% | +0.00% | 8,846,322 | 5 | 1 | 20.0 |
| default | 1 (1) | 413.84 | 413.84 ± 0.00 | 413.8389 | +0.00% | +35.61% | 2,801,842 | 4,518,999 | 4,494,622 | 99.5 |
| leapc | 5 (5) | 415.25 | 413.40 ± 5.74 | 403.3243 / 416.6974 / 414.4623 / 415.2510 / 417.2585 | -0.34% | +35.15% | 1,838,342 | 7,015,678 | 6,981,110 | 99.5 |
| n4_c1 | 1 (1) | 546.81 | 546.81 ± 0.00 | 546.8083 | -24.32% | +2.63% | 4,547,513 | 4,163,011 | 4,147,057 | 99.6 |
| n4_c0 | 1 (1) | 360.92 | 360.92 ± 0.00 | 360.9165 | +14.66% | +55.50% | 1,683,863 | 6,538,472 | 6,512,193 | 99.6 |
| n32_c1 | 1 (1) | 420.83 | 420.83 ± 0.00 | 420.8307 | -1.66% | +33.36% | 3,633,287 | 4,918,868 | 4,904,481 | 99.7 |
| n32_c0 | 1 (1) | 296.92 | 296.92 ± 0.00 | 296.9227 | +39.38% | +89.01% | 623,511 | 8,058,000 | 8,025,044 | 99.6 |

## etc

> Redis + memtier_benchmark, Facebook ETC/SYS Memcached pools (Atikoglu et al., SIGMETRICS '12) as run by
> Infiniswap (NSDI '17) and Leap (ATC '20) with mutilate: uniform random keys (mutilate's default),
> ETC = 95% GET / 5% SET, SYS = 75% GET / 25% SET; value sizes = mutilate fb_value, Generalized Pareto
> (loc 15, scale 214.476, shape 0.348238): 1000 stratified quantiles, shuffled (seed 42), weight 1 each.
> (memtier walks --data-size-list in order, repeating each size weight times, so weights must be 1.)
> Test = ETC: 10M keys, 95% GET (--ratio=1:19), 10000 requests per client (4 threads x 50 clients), --distinct-client-seed (one seed per client; without it all 200 clients send the same keys).
> Load: --ratio=1:0 --key-pattern=P:P -n allkeys. Server in the cgroup (redis 5.0.7, as wl/redis.sh), memtier native.
> Metric = memtier Totals ops/sec (p99 latency is in meas.log). Valid = memtier exited 0, Totals line, no errors.

metric: `ops_per_sec` (higher is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh etc noprefetch 1: cgroup /sys/fs/cgroup/swappf_etc memory.max=2384461824; vm.page-cluster=3; insmod /data1/shaurya/deku_etc/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh etc default 1: cgroup /sys/fs/cgroup/swappf_etc memory.max=2384461824; vm.page-cluster=3; insmod /data1/shaurya/deku_etc/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh etc leapc 1: cgroup /sys/fs/cgroup/swappf_etc memory.max=2384461824; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh etc n4_c0 1: cgroup /sys/fs/cgroup/swappf_etc memory.max=2384461824; vm.page-cluster=3; insmod /data1/shaurya/deku_etc/linux66b/eden_swappf_n4_c0.ko next_n=4 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh etc n32_c0 1: cgroup /sys/fs/cgroup/swappf_etc memory.max=2384461824; vm.page-cluster=3; insmod /data1/shaurya/deku_etc/linux66b/eden_swappf_n32_c0.ko next_n=32 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
command: server (in cgroup): cd /data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src; ./redis-server --port 6390 --save '' --appendonly no | load (native): /data1/shaurya/deku_memtier/install/bin/memtier_benchmark -s 127.0.0.1 -p 6390 -t 4 -c 50 --data-size-list=437:1,187:1,751:1,900:1,22:1,174:1,34:1,398:1,116:1,196:1,60:1,496:1,143:1,258:1,317:1,184:1,412:1,26:1,374:1,110:1,158:1,461:1,115:1,166:1,264:1,171:1,257:1,215:1,121:1,427:1,270:1,1044:1,473:1,181:1,914:1,122:1,244:1,359:1,27:1,147:1,28:1,85:1,431:1,61:1,166:1,459:1,58:1,51:1,386:1,41:1,147:1,1388:1,19:1,62:1,141:1,239:1,302:1,417:1,71:1,237:1,233:1,96:1,277:1,129:1,120:1,686:1,109:1,19:1,65:1,15:1,23:1,93:1,180:1,79:1,187:1,756:1,351:1,734:1,32:1,832:1,464:1,188:1,58:1,571:1,65:1,420:1,821:1,94:1,205:1,20:1,330:1,68:1,146:1,199:1,23:1,943:1,134:1,252:1,96:1,587:1,304:1,90:1,1035:1,1085:1,251:1,1280:1,990:1,373:1,92:1,199:1,492:1,74:1,151:1,21:1,124:1,517:1,102:1,219:1,77:1,51:1,192:1,143:1,296:1,64:1,236:1,106:1,526:1,339:1,1141:1,192:1,634:1,556:1,679:1,118:1,242:1,457:1,61:1,30:1,117:1,477:1,784:1,887:1,1064:1,107:1,368:1,133:1,164:1,132:1,100:1,315:1,390:1,893:1,163:1,196:1,271:1,178:1,63:1,82:1,329:1,384:1,230:1,53:1,256:1,132:1,1476:1,324:1,119:1,511:1,136:1,36:1,867:1,177:1,158:1,80:1,290:1,426:1,306:1,45:1,276:1,2639:1,112:1,99:1,47:1,35:1,106:1,170:1,1870:1,593:1,738:1,125:1,59:1,21:1,114:1,183:1,170:1,260:1,333:1,26:1,68:1,127:1,701:1,57:1,50:1,143:1,1314:1,50:1,1640:1,33:1,435:1,97:1,3442:1,198:1,434:1,74:1,266:1,479:1,220:1,188:1,566:1,180:1,43:1,171:1,49:1,21:1,826:1,43:1,251:1,29:1,658:1,227:1,16:1,928:1,278:1,153:1,370:1,63:1,401:1,267:1,226:1,19:1,387:1,849:1,189:1,228:1,48:1,62:1,16:1,154:1,19:1,69:1,29:1,140:1,97:1,108:1,63:1,300:1,84:1,285:1,498:1,74:1,470:1,316:1,486:1,174:1,119:1,561:1,76:1,268:1,1249:1,234:1,87:1,65:1,18:1,147:1,77:1,34:1,202:1,57:1,93:1,175:1,107:1,318:1,103:1,160:1,80:1,582:1,89:1,160:1,780:1,414:1,211:1,59:1,217:1,269:1,145:1,574:1,17:1,65:1,146:1,203:1,22:1,232:1,137:1,89:1,155:1,429:1,124:1,362:1,135:1,33:1,205:1,229:1,211:1,347:1,24:1,709:1,272:1,616:1,48:1,129:1,161:1,974:1,504:1,48:1,265:1,104:1,345:1,161:1,664:1,106:1,45:1,46:1,513:1,134:1,855:1,2956:1,142:1,243:1,83:1,101:1,139:1,54:1,98:1,92:1,282:1,274:1,116:1,30:1,313:1,549:1,999:1,314:1,358:1,76:1,85:1,36:1,554:1,628:1,27:1,294:1,1074:1,138:1,127:1,44:1,179:1,483:1,775:1,148:1,1096:1,340:1,95:1,682:1,68:1,874:1,23:1,622:1,765:1,44:1,20:1,564:1,1673:1,38:1,42:1,151:1,1153:1,43:1,128:1,26:1,57:1,103:1,105:1,690:1,84:1,50:1,25:1,186:1,35:1,125:1,201:1,2028:1,596:1,113:1,136:1,73:1,2232:1,55:1,109:1,247:1,861:1,169:1,156:1,40:1,610:1,52:1,162:1,90:1,67:1,52:1,237:1,408:1,2089:1,128:1,481:1,286:1,490:1,204:1,185:1,380:1,219:1,209:1,117:1,67:1,721:1,255:1,349:1,201:1,83:1,52:1,599:1,29:1,150:1,111:1,191:1,395:1,176:1,197:1,743:1,1179:1,61:1,309:1,112:1,126:1,789:1,122:1,224:1,88:1,326:1,85:1,447:1,1017:1,202:1,20:1,208:1,37:1,95:1,163:1,64:1,344:1,84:1,100:1,1784:1,54:1,136:1,206:1,58:1,366:1,466:1,515:1,440:1,406:1,96:1,36:1,950:1,157:1,283:1,18:1,173:1,3812:1,647:1,149:1,331:1,185:1,83:1,107:1,59:1,222:1,133:1,181:1,23:1,154:1,935:1,295:1,275:1,38:1,110:1,18:1,319:1,42:1,156:1,138:1,509:1,101:1,118:1,168:1,66:1,73:1,173:1,24:1,131:1,41:1,547:1,502:1,1331:1,144:1,111:1,60:1,4361:1,145:1,59:1,795:1,625:1,286:1,391:1,336:1,39:1,880:1,288:1,121:1,261:1,800:1,102:1,24:1,46:1,153:1,51:1,238:1,23:1,52:1,312:1,1297:1,248:1,152:1,159:1,452:1,86:1,47:1,111:1,484:1,25:1,91:1,289:1,1129:1,27:1,521:1,442:1,1708:1,246:1,67:1,115:1,69:1,93:1,89:1,1192:1,519:1,40:1,43:1,1054:1,48:1,40:1,99:1,1552:1,17:1,524:1,100:1,217:1,388:1,274:1,94:1,126:1,725:1,269:1,118:1,350:1,262:1,194:1,298:1,213:1,32:1,39:1,230:1,613:1,17:1,32:1,1166:1,167:1,99:1,47:1,619:1,705:1,78:1,279:1,68:1,86:1,654:1,204:1,231:1,579:1,1452:1,34:1,24:1,253:1,241:1,601:1,235:1,44:1,141:1,77:1,363:1,254:1,421:1,62:1,34:1,195:1,675:1,488:1,448:1,191:1,177:1,585:1,308:1,28:1,104:1,327:1,216:1,242:1,559:1,71:1,98:1,354:1,53:1,182:1,225:1,761:1,94:1,165:1,2315:1,135:1,16:1,713:1,56:1,365:1,651:1,70:1,179:1,353:1,399:1,56:1,78:1,1826:1,18:1,215:1,303:1,26:1,2409:1,221:1,159:1,33:1,82:1,838:1,356:1,61:1,539:1,49:1,1409:1,506:1,342:1,907:1,81:1,41:1,114:1,140:1,29:1,38:1,297:1,305:1,325:1,27:1,172:1,144:1,405:1,120:1,154:1,310:1,1107:1,37:1,63:1,142:1,346:1,79:1,175:1,240:1,235:1,47:1,402:1,71:1,423:1,321:1,91:1,78:1,198:1,369:1,110:1,661:1,31:1,668:1,29:1,284:1,364:1,31:1,17:1,1430:1,376:1,75:1,224:1,248:1,20:1,113:1,535:1,74:1,81:1,44:1,164:1,200:1,157:1,544:1,1525:1,323:1,577:1,103:1,371:1,66:1,343:1,1026:1,450:1,641:1,150:1,263:1,207:1,607:1,56:1,220:1,176:1,311:1,46:1,45:1,210:1,31:1,182:1,396:1,35:1,982:1,638:1,79:1,28:1,78:1,102:1,123:1,494:1,19:1,183:1,108:1,257:1,15:1,208:1,214:1,55:1,475:1,128:1,551:1,1580:1,747:1,471:1,8090:1,98:1,279:1,40:1,194:1,60:1,443:1,190:1,958:1,379:1,383:1,16:1,815:1,54:1,155:1,966:1,5327:1,1220:1,293:1,455:1,770:1,424:1,1008:1,334:1,42:1,382:1,542:1,17:1,223:1,82:1,207:1,172:1,1350:1,131:1,133:1,30:1,254:1,1234:1,332:1,500:1,1264:1,280:1,53:1,41:1,26:1,432:1,36:1,186:1,195:1,49:1,694:1,921:1,139:1,245:1,152:1,245:1,418:1,86:1,213:1,1609:1,415:1,81:1,2784:1,87:1,50:1,167:1,294:1,137:1,190:1,307:1,70:1,104:1,378:1,233:1,70:1,30:1,88:1,139:1,105:1,528:1,1919:1,569:1,75:1,28:1,468:1,454:1,631:1,108:1,337:1,72:1,226:1,341:1,292:1,88:1,130:1,169:1,56:1,3170:1,392:1,210:1,58:1,810:1,265:1,32:1,299:1,335:1,1500:1,87:1,322:1,69:1,120:1,126:1,55:1,124:1,604:1,287:1,165:1,1206:1,1118:1,39:1,717:1,1744:1,671:1,34:1,95:1,462:1,75:1,312:1,25:1,31:1,360:1,64:1,240:1,123:1,697:1,805:1,273:1,281:1,590:1,97:1,222:1,33:1,130:1,45:1,212:1,168:1,394:1,25:1,533:1,86:1,261:1,116:1,644:1,122:1,38:1,130:1,37:1,2516:1,113:1,445:1,71:1,53:1,1369:1,114:1,1971:1,352:1,55:1,530:1,439:1,16:1,730:1,537:1,91:1,249:1,162:1,72:1,148:1,2157:1,357:1,301:1,375:1,66:1,228:1,21:1,259:1,193:1,76:1,72:1,37:1,22:1,22:1,149:1,250:1,35:1,218:1,843:1,409:1,328:1,39:1,404:1,49:1,73:1,80:1,90:1,411:1,21:1,42:1,291:1 --key-minimum=1 --key-maximum=10000000 --hide-histogram --ratio=1:0 --key-pattern=P:P -n allkeys | measured (native): /data1/shaurya/deku_memtier/install/bin/memtier_benchmark -s 127.0.0.1 -p 6390 -t 4 -c 50 --data-size-list=437:1,187:1,751:1,900:1,22:1,174:1,34:1,398:1,116:1,196:1,60:1,496:1,143:1,258:1,317:1,184:1,412:1,26:1,374:1,110:1,158:1,461:1,115:1,166:1,264:1,171:1,257:1,215:1,121:1,427:1,270:1,1044:1,473:1,181:1,914:1,122:1,244:1,359:1,27:1,147:1,28:1,85:1,431:1,61:1,166:1,459:1,58:1,51:1,386:1,41:1,147:1,1388:1,19:1,62:1,141:1,239:1,302:1,417:1,71:1,237:1,233:1,96:1,277:1,129:1,120:1,686:1,109:1,19:1,65:1,15:1,23:1,93:1,180:1,79:1,187:1,756:1,351:1,734:1,32:1,832:1,464:1,188:1,58:1,571:1,65:1,420:1,821:1,94:1,205:1,20:1,330:1,68:1,146:1,199:1,23:1,943:1,134:1,252:1,96:1,587:1,304:1,90:1,1035:1,1085:1,251:1,1280:1,990:1,373:1,92:1,199:1,492:1,74:1,151:1,21:1,124:1,517:1,102:1,219:1,77:1,51:1,192:1,143:1,296:1,64:1,236:1,106:1,526:1,339:1,1141:1,192:1,634:1,556:1,679:1,118:1,242:1,457:1,61:1,30:1,117:1,477:1,784:1,887:1,1064:1,107:1,368:1,133:1,164:1,132:1,100:1,315:1,390:1,893:1,163:1,196:1,271:1,178:1,63:1,82:1,329:1,384:1,230:1,53:1,256:1,132:1,1476:1,324:1,119:1,511:1,136:1,36:1,867:1,177:1,158:1,80:1,290:1,426:1,306:1,45:1,276:1,2639:1,112:1,99:1,47:1,35:1,106:1,170:1,1870:1,593:1,738:1,125:1,59:1,21:1,114:1,183:1,170:1,260:1,333:1,26:1,68:1,127:1,701:1,57:1,50:1,143:1,1314:1,50:1,1640:1,33:1,435:1,97:1,3442:1,198:1,434:1,74:1,266:1,479:1,220:1,188:1,566:1,180:1,43:1,171:1,49:1,21:1,826:1,43:1,251:1,29:1,658:1,227:1,16:1,928:1,278:1,153:1,370:1,63:1,401:1,267:1,226:1,19:1,387:1,849:1,189:1,228:1,48:1,62:1,16:1,154:1,19:1,69:1,29:1,140:1,97:1,108:1,63:1,300:1,84:1,285:1,498:1,74:1,470:1,316:1,486:1,174:1,119:1,561:1,76:1,268:1,1249:1,234:1,87:1,65:1,18:1,147:1,77:1,34:1,202:1,57:1,93:1,175:1,107:1,318:1,103:1,160:1,80:1,582:1,89:1,160:1,780:1,414:1,211:1,59:1,217:1,269:1,145:1,574:1,17:1,65:1,146:1,203:1,22:1,232:1,137:1,89:1,155:1,429:1,124:1,362:1,135:1,33:1,205:1,229:1,211:1,347:1,24:1,709:1,272:1,616:1,48:1,129:1,161:1,974:1,504:1,48:1,265:1,104:1,345:1,161:1,664:1,106:1,45:1,46:1,513:1,134:1,855:1,2956:1,142:1,243:1,83:1,101:1,139:1,54:1,98:1,92:1,282:1,274:1,116:1,30:1,313:1,549:1,999:1,314:1,358:1,76:1,85:1,36:1,554:1,628:1,27:1,294:1,1074:1,138:1,127:1,44:1,179:1,483:1,775:1,148:1,1096:1,340:1,95:1,682:1,68:1,874:1,23:1,622:1,765:1,44:1,20:1,564:1,1673:1,38:1,42:1,151:1,1153:1,43:1,128:1,26:1,57:1,103:1,105:1,690:1,84:1,50:1,25:1,186:1,35:1,125:1,201:1,2028:1,596:1,113:1,136:1,73:1,2232:1,55:1,109:1,247:1,861:1,169:1,156:1,40:1,610:1,52:1,162:1,90:1,67:1,52:1,237:1,408:1,2089:1,128:1,481:1,286:1,490:1,204:1,185:1,380:1,219:1,209:1,117:1,67:1,721:1,255:1,349:1,201:1,83:1,52:1,599:1,29:1,150:1,111:1,191:1,395:1,176:1,197:1,743:1,1179:1,61:1,309:1,112:1,126:1,789:1,122:1,224:1,88:1,326:1,85:1,447:1,1017:1,202:1,20:1,208:1,37:1,95:1,163:1,64:1,344:1,84:1,100:1,1784:1,54:1,136:1,206:1,58:1,366:1,466:1,515:1,440:1,406:1,96:1,36:1,950:1,157:1,283:1,18:1,173:1,3812:1,647:1,149:1,331:1,185:1,83:1,107:1,59:1,222:1,133:1,181:1,23:1,154:1,935:1,295:1,275:1,38:1,110:1,18:1,319:1,42:1,156:1,138:1,509:1,101:1,118:1,168:1,66:1,73:1,173:1,24:1,131:1,41:1,547:1,502:1,1331:1,144:1,111:1,60:1,4361:1,145:1,59:1,795:1,625:1,286:1,391:1,336:1,39:1,880:1,288:1,121:1,261:1,800:1,102:1,24:1,46:1,153:1,51:1,238:1,23:1,52:1,312:1,1297:1,248:1,152:1,159:1,452:1,86:1,47:1,111:1,484:1,25:1,91:1,289:1,1129:1,27:1,521:1,442:1,1708:1,246:1,67:1,115:1,69:1,93:1,89:1,1192:1,519:1,40:1,43:1,1054:1,48:1,40:1,99:1,1552:1,17:1,524:1,100:1,217:1,388:1,274:1,94:1,126:1,725:1,269:1,118:1,350:1,262:1,194:1,298:1,213:1,32:1,39:1,230:1,613:1,17:1,32:1,1166:1,167:1,99:1,47:1,619:1,705:1,78:1,279:1,68:1,86:1,654:1,204:1,231:1,579:1,1452:1,34:1,24:1,253:1,241:1,601:1,235:1,44:1,141:1,77:1,363:1,254:1,421:1,62:1,34:1,195:1,675:1,488:1,448:1,191:1,177:1,585:1,308:1,28:1,104:1,327:1,216:1,242:1,559:1,71:1,98:1,354:1,53:1,182:1,225:1,761:1,94:1,165:1,2315:1,135:1,16:1,713:1,56:1,365:1,651:1,70:1,179:1,353:1,399:1,56:1,78:1,1826:1,18:1,215:1,303:1,26:1,2409:1,221:1,159:1,33:1,82:1,838:1,356:1,61:1,539:1,49:1,1409:1,506:1,342:1,907:1,81:1,41:1,114:1,140:1,29:1,38:1,297:1,305:1,325:1,27:1,172:1,144:1,405:1,120:1,154:1,310:1,1107:1,37:1,63:1,142:1,346:1,79:1,175:1,240:1,235:1,47:1,402:1,71:1,423:1,321:1,91:1,78:1,198:1,369:1,110:1,661:1,31:1,668:1,29:1,284:1,364:1,31:1,17:1,1430:1,376:1,75:1,224:1,248:1,20:1,113:1,535:1,74:1,81:1,44:1,164:1,200:1,157:1,544:1,1525:1,323:1,577:1,103:1,371:1,66:1,343:1,1026:1,450:1,641:1,150:1,263:1,207:1,607:1,56:1,220:1,176:1,311:1,46:1,45:1,210:1,31:1,182:1,396:1,35:1,982:1,638:1,79:1,28:1,78:1,102:1,123:1,494:1,19:1,183:1,108:1,257:1,15:1,208:1,214:1,55:1,475:1,128:1,551:1,1580:1,747:1,471:1,8090:1,98:1,279:1,40:1,194:1,60:1,443:1,190:1,958:1,379:1,383:1,16:1,815:1,54:1,155:1,966:1,5327:1,1220:1,293:1,455:1,770:1,424:1,1008:1,334:1,42:1,382:1,542:1,17:1,223:1,82:1,207:1,172:1,1350:1,131:1,133:1,30:1,254:1,1234:1,332:1,500:1,1264:1,280:1,53:1,41:1,26:1,432:1,36:1,186:1,195:1,49:1,694:1,921:1,139:1,245:1,152:1,245:1,418:1,86:1,213:1,1609:1,415:1,81:1,2784:1,87:1,50:1,167:1,294:1,137:1,190:1,307:1,70:1,104:1,378:1,233:1,70:1,30:1,88:1,139:1,105:1,528:1,1919:1,569:1,75:1,28:1,468:1,454:1,631:1,108:1,337:1,72:1,226:1,341:1,292:1,88:1,130:1,169:1,56:1,3170:1,392:1,210:1,58:1,810:1,265:1,32:1,299:1,335:1,1500:1,87:1,322:1,69:1,120:1,126:1,55:1,124:1,604:1,287:1,165:1,1206:1,1118:1,39:1,717:1,1744:1,671:1,34:1,95:1,462:1,75:1,312:1,25:1,31:1,360:1,64:1,240:1,123:1,697:1,805:1,273:1,281:1,590:1,97:1,222:1,33:1,130:1,45:1,212:1,168:1,394:1,25:1,533:1,86:1,261:1,116:1,644:1,122:1,38:1,130:1,37:1,2516:1,113:1,445:1,71:1,53:1,1369:1,114:1,1971:1,352:1,55:1,530:1,439:1,16:1,730:1,537:1,91:1,249:1,162:1,72:1,148:1,2157:1,357:1,301:1,375:1,66:1,228:1,21:1,259:1,193:1,76:1,72:1,37:1,22:1,22:1,149:1,250:1,35:1,218:1,843:1,409:1,328:1,39:1,404:1,49:1,73:1,80:1,90:1,411:1,21:1,42:1,291:1 --key-minimum=1 --key-maximum=10000000 --hide-histogram --ratio=1:19 --key-pattern=R:R -n 10000 --distinct-client-seed
```

| config | runs (valid) | ops_per_sec median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 11514.37 | 11508.18 ± 41.80 | 11546.53 / 11514.37 / 11463.63 | +1.56% | +0.00% | 1,943,521 | 0 | 0 | - |
| default | 3 (3) | 11337.21 | 11318.78 ± 43.88 | 11337.21 / 11350.45 / 11268.69 | +0.00% | -1.54% | 1,938,991 | 46,889 | 14,823 | 31.6 |
| leapc | 5 (5) | 8403.73 | 8398.79 ± 19.39 | 8383.19 / 8403.73 / 8374.71 / 8421.29 / 8411.02 | -25.87% | -27.02% | 1,867,170 | 4,852,875 | 1,176,666 | 24.2 |
| n4_c0 | 3 (3) | 11387.79 | 11384.56 ± 9.72 | 11387.79 / 11392.26 / 11373.64 | +0.45% | -1.10% | 1,930,398 | 115,339 | 48,345 | 41.9 |
| n32_c0 | 3 (3) | 10518.14 | 10512.91 ± 15.32 | 10518.14 / 10524.94 / 10495.66 | -7.22% | -8.65% | 1,956,479 | 770,423 | 241,578 | 31.4 |

## gap_pr

> GAPBS pr on twitter.sg (test input; trees = gap_pr_lj's, trained on LiveJournal), 1 trial(s), OMP_NUM_THREADS=16.
> Metric = GAPBS "Average Time" (the kernel trials, graph loading excluded); elapsed_s = whole process.
> Valid = -a stats vs native (wl/gap_check.py).

metric: `avg_trial_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh gap_pr noprefetch 1: cgroup /sys/fs/cgroup/swappf_gap_pr memory.max=6736052224; vm.page-cluster=3; insmod /data1/shaurya/deku_gap_pr/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh gap_pr default 1: cgroup /sys/fs/cgroup/swappf_gap_pr memory.max=6736052224; vm.page-cluster=3; insmod /data1/shaurya/deku_gap_pr/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh gap_pr leapc 1: cgroup /sys/fs/cgroup/swappf_gap_pr memory.max=6736052224; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh gap_pr n4_c0 1: cgroup /sys/fs/cgroup/swappf_gap_pr memory.max=6736052224; vm.page-cluster=3; insmod /data1/shaurya/deku_gap_pr/linux66b/eden_swappf_n4_c0.ko next_n=4 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh gap_pr n32_c0 1: cgroup /sys/fs/cgroup/swappf_gap_pr memory.max=6736052224; vm.page-cluster=3; insmod /data1/shaurya/deku_gap_pr/linux66b/eden_swappf_n32_c0.ko next_n=32 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_gap_pr/linux66b/runs/noprefetch_r1) /data1/shaurya/deku_gapbs/gapbs/pr -f /data1/shaurya/deku_gapbs/gapbs/benchmark/graphs/twitter.sg -n1 -a  > stdout.out 2> stderr.log
```

| config | runs (valid) | avg_trial_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 117.18 | 118.04 ± 2.95 | 121.3149 / 115.6088 / 117.1848 | -54.88% | +0.00% | 14,393,362 | 0 | 771 | - |
| default | 3 (3) | 52.87 | 52.65 ± 1.35 | 51.20169 / 52.87141 / 53.86308 | +0.00% | +121.64% | 2,940,518 | 10,494,435 | 10,423,671 | 99.3 |
| leapc | 5 (5) | 50.22 | 50.07 ± 2.59 | 50.21725 / 48.89786 / 50.42857 / 46.86220 / 53.94540 | +5.29% | +133.36% | 2,404,566 | 10,835,260 | 10,836,614 | 100.0 |
| n4_c0 | 3 (3) | 48.68 | 49.52 ± 3.03 | 48.68314 / 52.87557 / 46.99218 | +8.60% | +140.71% | 2,645,421 | 10,124,054 | 10,126,245 | 100.0 |
| n32_c0 | 3 (3) | 34.32 | 34.99 ± 1.28 | 34.32379 / 34.18138 / 36.46143 | +54.04% | +241.41% | 590,865 | 12,384,962 | 12,389,203 | 100.0 |

## gap_sssp

> GAPBS sssp on twitter.wsg (test input; trees = gap_sssp_lj's, trained on LiveJournal), 1 trial(s), OMP_NUM_THREADS=16.
> Metric = GAPBS "Average Time" (the kernel trials, graph loading excluded); elapsed_s = whole process.
> Valid = -a stats vs native (wl/gap_check.py).

metric: `avg_trial_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh gap_sssp noprefetch 1: cgroup /sys/fs/cgroup/swappf_gap_sssp memory.max=12580814848; vm.page-cluster=3; insmod /data1/shaurya/deku_gap_sssp/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh gap_sssp default 1: cgroup /sys/fs/cgroup/swappf_gap_sssp memory.max=12580814848; vm.page-cluster=3; insmod /data1/shaurya/deku_gap_sssp/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh gap_sssp leapc 1: cgroup /sys/fs/cgroup/swappf_gap_sssp memory.max=12580814848; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh gap_sssp n4_c0 1: cgroup /sys/fs/cgroup/swappf_gap_sssp memory.max=12580814848; vm.page-cluster=3; insmod /data1/shaurya/deku_gap_sssp/linux66b/eden_swappf_n4_c0.ko next_n=4 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh gap_sssp n32_c0 1: cgroup /sys/fs/cgroup/swappf_gap_sssp memory.max=12580814848; vm.page-cluster=3; insmod /data1/shaurya/deku_gap_sssp/linux66b/eden_swappf_n32_c0.ko next_n=32 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_gap_sssp/linux66b/runs/noprefetch_r1) /data1/shaurya/deku_gapbs/gapbs/sssp -f /data1/shaurya/deku_gapbs/gapbs/benchmark/graphs/twitter.wsg -n1 -a -d2 > stdout.out 2> stderr.log
```

| config | runs (valid) | avg_trial_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 37.96 | 37.80 ± 0.62 | 37.11930 / 37.96274 / 38.32369 | -29.26% | +0.00% | 3,224,566 | 0 | 119 | - |
| default | 3 (3) | 26.85 | 27.14 ± 1.39 | 26.85340 / 25.92223 / 28.65566 | +0.00% | +41.37% | 1,158,908 | 2,454,924 | 2,045,429 | 83.3 |
| leapc | 5 (5) | 25.82 | 25.65 ± 0.52 | 25.88042 / 24.88306 / 26.24572 / 25.41720 / 25.82490 | +3.98% | +47.00% | 848,563 | 2,831,554 | 2,339,855 | 82.6 |
| n4_c0 | 3 (3) | 32.26 | 31.92 ± 0.61 | 32.27877 / 31.20817 / 32.25834 | -16.76% | +17.68% | 2,287,462 | 940,260 | 885,687 | 94.2 |
| n32_c0 | 3 (3) | 25.75 | 25.55 ± 0.45 | 25.86473 / 25.02961 / 25.74844 | +4.29% | +47.44% | 1,158,292 | 2,526,392 | 2,053,307 | 81.3 |

## lbm_ref

> SPEC 519.lbm_r, refrate input (3000 steps, same 100x100x130 grid). Trees = the lbm (train-input) trees, symlinked from /data1/shaurya/deku_lbm/linux66b.
> Valid = specdiff -a 1e-7 (SPEC's abstol) vs SPEC's refrate lbm.out.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh lbm_ref noprefetch 1: cgroup /sys/fs/cgroup/swappf_lbm_ref memory.max=214958080; vm.page-cluster=3; insmod /data1/shaurya/deku_lbm_ref/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh lbm_ref default 1: cgroup /sys/fs/cgroup/swappf_lbm_ref memory.max=214958080; vm.page-cluster=3; insmod /data1/shaurya/deku_lbm_ref/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh lbm_ref leapc 1: cgroup /sys/fs/cgroup/swappf_lbm_ref memory.max=214958080; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh lbm_ref n4_c0 1: cgroup /sys/fs/cgroup/swappf_lbm_ref memory.max=214958080; vm.page-cluster=3; insmod /data1/shaurya/deku_lbm_ref/linux66b/eden_swappf_n4_c0.ko next_n=4 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh lbm_ref n32_c0 1: cgroup /sys/fs/cgroup/swappf_lbm_ref memory.max=214958080; vm.page-cluster=3; insmod /data1/shaurya/deku_lbm_ref/linux66b/eden_swappf_n32_c0.ko next_n=32 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_lbm_ref/linux66b/runs/noprefetch_r1) /data1/shaurya/spec2017/lbm_build/lbm 3000 reference.dat 0 0 100_100_130_ldc.of > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 6654.75 | 6728.32 ± 129.59 | 6652.251 / 6877.947 / 6654.750 | -47.56% | +0.00% | 242,851,738 | 0 | 2 | - |
| default | 3 (3) | 3489.89 | 3488.46 ± 21.91 | 3509.614 / 3465.865 / 3489.891 | +0.00% | +90.69% | 45,104,283 | 227,270,245 | 226,213,425 | 99.5 |
| leapc | 5 (5) | 2454.77 | 2460.16 ± 37.94 | 2482.798 / 2422.162 / 2454.771 / 2428.350 / 2512.715 | +42.17% | +171.09% | 36,587,891 | 210,690,605 | 210,625,050 | 100.0 |
| n4_c0 | 3 (3) | 3213.10 | 3257.46 ± 78.55 | 3348.155 / 3213.102 / 3211.110 | +8.61% | +107.11% | 75,178,634 | 169,699,399 | 169,683,491 | 100.0 |
| n32_c0 | 3 (3) | 2519.01 | 2516.81 ± 11.28 | 2504.595 / 2526.825 / 2519.009 | +38.54% | +164.18% | 14,692,021 | 233,814,079 | 233,714,617 | 100.0 |

## mcf

> SPEC 505.mcf_r, train input, 273MB (as eden). Trace = same input. Valid = stdout + mcf.out == native.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh mcf noprefetch 1: cgroup /sys/fs/cgroup/swappf_mcf memory.max=286261248; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh mcf default 1: cgroup /sys/fs/cgroup/swappf_mcf memory.max=286261248; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh mcf leapc 1: cgroup /sys/fs/cgroup/swappf_mcf memory.max=286261248; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh mcf n4_c1 1: cgroup /sys/fs/cgroup/swappf_mcf memory.max=286261248; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh mcf n4_c0 1: cgroup /sys/fs/cgroup/swappf_mcf memory.max=286261248; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh mcf n32_c1 1: cgroup /sys/fs/cgroup/swappf_mcf memory.max=286261248; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh mcf n32_c0 1: cgroup /sys/fs/cgroup/swappf_mcf memory.max=286261248; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_mcf/linux66b/runs/noprefetch_r1) /data1/shaurya/spec2017/mcf_build/mcf /data1/shaurya/spec2017/mcf_build/inp_train.in > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 5 (5) | 91.58 | 91.62 ± 0.53 | 90.88835 / 91.58415 / 91.36797 / 92.26112 / 91.97458 | -9.62% | +0.00% | 1,356,738 | 0 | 1 | - |
| default | 5 (5) | 82.77 | 82.87 ± 0.66 | 82.26364 / 83.22605 / 83.80854 / 82.76997 / 82.27132 | +0.00% | +10.65% | 756,625 | 745,670 | 672,668 | 90.2 |
| leapc | 5 (5) | 79.88 | 79.85 ± 0.82 | 80.54545 / 79.87974 / 78.49967 / 79.85977 / 80.45763 | +3.62% | +14.65% | 513,109 | 1,113,295 | 995,933 | 89.5 |
| n4_c1 | 5 (5) | 85.13 | 85.73 ± 1.57 | 85.11990 / 88.50821 / 84.63805 / 85.25343 / 85.13431 | -2.78% | +7.58% | 1,007,793 | 382,878 | 371,190 | 96.9 |
| n4_c0 | 5 (5) | 83.21 | 83.33 ± 0.42 | 83.44967 / 84.00873 / 82.95961 / 83.20557 / 83.03979 | -0.52% | +10.07% | 806,879 | 623,337 | 600,163 | 96.3 |
| n32_c1 | 5 (5) | 84.77 | 84.46 ± 0.69 | 84.22149 / 85.14450 / 83.36625 / 84.77026 / 84.78814 | -2.36% | +8.04% | 801,821 | 699,794 | 638,547 | 91.2 |
| n32_c0 | 5 (5) | 78.58 | 78.60 ± 0.24 | 78.38002 / 78.62421 / 78.99873 / 78.58124 / 78.43471 | +5.33% | +16.55% | 550,423 | 1,024,982 | 935,093 | 91.2 |

## mcf_ref

> SPEC 505.mcf_r, refrate input (inp_refrate.in), 306MB = 50% of native peak (611MB). Trees = the mcf (train-input) trees,
> symlinked from /data1/shaurya/deku_mcf/linux66b. Valid = stdout + mcf.out == native refrate.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh mcf_ref noprefetch 1: cgroup /sys/fs/cgroup/swappf_mcf_ref memory.max=320864256; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf_ref/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh mcf_ref default 1: cgroup /sys/fs/cgroup/swappf_mcf_ref memory.max=320864256; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf_ref/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh mcf_ref leapc 1: cgroup /sys/fs/cgroup/swappf_mcf_ref memory.max=320864256; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh mcf_ref n4_c1 1: cgroup /sys/fs/cgroup/swappf_mcf_ref memory.max=320864256; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf_ref/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh mcf_ref n4_c0 1: cgroup /sys/fs/cgroup/swappf_mcf_ref memory.max=320864256; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf_ref/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh mcf_ref n32_c1 1: cgroup /sys/fs/cgroup/swappf_mcf_ref memory.max=320864256; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf_ref/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh mcf_ref n32_c0 1: cgroup /sys/fs/cgroup/swappf_mcf_ref memory.max=320864256; vm.page-cluster=3; insmod /data1/shaurya/deku_mcf_ref/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_mcf_ref/linux66b/runs/noprefetch_r1) /data1/shaurya/spec2017/mcf_build/mcf /data1/shaurya/spec2017/mcf_build/inp_refrate.in > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 1554.51 | 1552.65 ± 9.84 | 1561.418 / 1554.505 / 1542.011 | -23.90% | +0.00% | 41,690,203 | 2 | 13 | 760.0 |
| default | 3 (3) | 1183.03 | 1187.80 ± 15.63 | 1205.262 / 1183.034 / 1175.107 | +0.00% | +31.40% | 18,634,564 | 26,934,915 | 25,215,835 | 93.6 |
| leapc | 5 (5) | 1065.08 | 1066.56 ± 2.78 | 1064.967 / 1063.671 / 1069.212 / 1065.079 / 1069.858 | +11.07% | +45.95% | 11,390,095 | 34,080,982 | 32,565,070 | 95.6 |
| n4_c1 | 2 (2) | 1358.00 | 1358.00 ± 4.22 | 1360.989 / 1355.017 | -12.88% | +14.47% | 27,550,502 | 14,584,430 | 14,419,052 | 98.9 |
| n4_c0 | 3 (3) | 1144.69 | 1148.12 ± 9.74 | 1144.688 / 1159.105 / 1140.556 | +3.35% | +35.80% | 19,295,168 | 23,328,308 | 22,975,957 | 98.5 |
| n32_c1 | 2 (2) | 1146.42 | 1146.42 ± 6.38 | 1141.912 / 1150.933 | +3.19% | +35.60% | 16,689,900 | 26,793,478 | 26,034,598 | 97.2 |
| n32_c0 | 3 (3) | 908.15 | 907.97 ± 3.28 | 911.1537 / 908.1490 / 904.6004 | +30.27% | +71.17% | 7,470,591 | 37,478,580 | 36,218,754 | 96.6 |

## memcached

> memcached 1.6.29 (4 threads) replaying Twitter cluster34 requests 0-100M; server in 3938MB
> (as eden), replay client native. Trace = held-out requests 100M-200M at 3537MB (as eden).
> Metric = replay kreq/s. Valid = 0 mismatches and hits == native (48775658).

metric: `kreq_per_sec` (higher is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh memcached noprefetch 1: cgroup /sys/fs/cgroup/swappf_memcached memory.max=4129292288; vm.page-cluster=3; insmod /data1/shaurya/deku_memcached/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh memcached default 1: cgroup /sys/fs/cgroup/swappf_memcached memory.max=4129292288; vm.page-cluster=3; insmod /data1/shaurya/deku_memcached/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh memcached leapc 1: cgroup /sys/fs/cgroup/swappf_memcached memory.max=4129292288; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh memcached n4_c1 1: cgroup /sys/fs/cgroup/swappf_memcached memory.max=4129292288; vm.page-cluster=3; insmod /data1/shaurya/deku_memcached/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh memcached n4_c0 1: cgroup /sys/fs/cgroup/swappf_memcached memory.max=4129292288; vm.page-cluster=3; insmod /data1/shaurya/deku_memcached/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh memcached n32_c1 1: cgroup /sys/fs/cgroup/swappf_memcached memory.max=4129292288; vm.page-cluster=3; insmod /data1/shaurya/deku_memcached/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh memcached n32_c0 1: cgroup /sys/fs/cgroup/swappf_memcached memory.max=4129292288; vm.page-cluster=3; insmod /data1/shaurya/deku_memcached/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: server (in cgroup): /data1/shaurya/deku_memcached/install/bin/memcached -p 11299 -U 0 -l 127.0.0.1 -t 4 -m 65536 -c 1024 -A | measured (native client): /data1/shaurya/deku_memcached/client/replay /data1/shaurya/deku_memcached/traces/c34_300m.bin 127.0.0.1 11299 4 100000000
```

| config | runs (valid) | kreq_per_sec median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 5 (5) | 685.30 | 683.74 ± 5.31 | 681.5 / 689.3 / 675.7 / 686.9 / 685.3 | -1.13% | +0.00% | 324,559 | 0 | 0 | - |
| default | 5 (5) | 693.10 | 692.36 ± 4.43 | 692.0 / 693.1 / 685.1 / 694.9 / 696.7 | +0.00% | +1.14% | 126,880 | 359,616 | 218,873 | 60.9 |
| leapc | 5 (5) | 695.70 | 695.68 ± 3.18 | 700.7 / 696.1 / 693.2 / 692.7 / 695.7 | +0.38% | +1.52% | 105,826 | 329,967 | 229,307 | 69.5 |
| n4_c1 | 5 (5) | 682.30 | 683.12 ± 3.19 | 682.3 / 679.0 / 686.4 / 681.6 / 686.3 | -1.56% | -0.44% | 235,977 | 114,398 | 89,275 | 78.0 |
| n4_c0 | 5 (5) | 692.50 | 690.84 ± 4.55 | 692.5 / 695.2 / 690.8 / 692.5 / 683.2 | -0.09% | +1.05% | 137,813 | 213,653 | 191,166 | 89.5 |
| n32_c1 | 5 (5) | 695.10 | 693.82 ± 4.70 | 696.5 / 689.6 / 695.1 / 699.5 / 688.4 | +0.29% | +1.43% | 150,435 | 244,106 | 179,205 | 73.4 |
| n32_c0 | 5 (5) | 695.20 | 693.94 ± 4.80 | 692.7 / 695.2 / 696.3 / 699.1 / 686.4 | +0.30% | +1.44% | 91,671 | 295,130 | 234,984 | 79.6 |

## memtier

> Redis + memtier_benchmark, following Hermit's Memcached workload (Qiao et al., NSDI '23, Table 1 and Sec. 6.1):
> Facebook USR-like key-value pairs (2-byte values, ~16-byte keys), keys Zipf, 99.8% GET / 0.2% SET.
> Test input (Hermit's: 32M KVs, Zipf 0.99): 32000000 keys, Zipf exponent 0.99, 20000 requests per client (4 threads x 50 clients).
> Load: --ratio=1:0 --key-pattern=P:P -n allkeys. Server in the cgroup (redis 5.0.7, as wl/redis.sh), memtier native.
> Metric = memtier Totals ops/sec (p99 latency is in meas.log). Valid = memtier exited 0, Totals line, no errors.

metric: `ops_per_sec` (higher is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh memtier noprefetch 1: cgroup /sys/fs/cgroup/swappf_memtier memory.max=1671430144; vm.page-cluster=3; insmod /data1/shaurya/deku_memtier/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh memtier default 1: cgroup /sys/fs/cgroup/swappf_memtier memory.max=1671430144; vm.page-cluster=3; insmod /data1/shaurya/deku_memtier/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh memtier leapc 1: cgroup /sys/fs/cgroup/swappf_memtier memory.max=1671430144; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh memtier n4_c0 1: cgroup /sys/fs/cgroup/swappf_memtier memory.max=1671430144; vm.page-cluster=3; insmod /data1/shaurya/deku_memtier/linux66b/eden_swappf_n4_c0.ko next_n=4 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh memtier n32_c0 1: cgroup /sys/fs/cgroup/swappf_memtier memory.max=1671430144; vm.page-cluster=3; insmod /data1/shaurya/deku_memtier/linux66b/eden_swappf_n32_c0.ko next_n=32 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
command: server (in cgroup): cd /data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src; ./redis-server --port 6390 --save '' --appendonly no | load (native): /data1/shaurya/deku_memtier/install/bin/memtier_benchmark -s 127.0.0.1 -p 6390 -t 4 -c 50 -d 2 --key-minimum=1 --key-maximum=32000000 --hide-histogram --ratio=1:0 --key-pattern=P:P -n allkeys | measured (native): /data1/shaurya/deku_memtier/install/bin/memtier_benchmark -s 127.0.0.1 -p 6390 -t 4 -c 50 -d 2 --key-minimum=1 --key-maximum=32000000 --hide-histogram --ratio=1:499 --key-pattern=Z:Z --key-zipf-exp=0.99 -n 20000
```

| config | runs (valid) | ops_per_sec median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 175532.28 | 176409.76 ± 2185.01 | 175532.2 / 174799.8 / 178897.1 | +0.91% | +0.00% | 11,821 | 0 | 0 | - |
| default | 3 (3) | 173943.96 | 174107.92 ± 846.00 | 173355.9 / 175023.9 / 173943.9 | +0.00% | -0.90% | 11,901 | 210 | 72 | 34.1 |
| leapc | 5 (5) | 175142.16 | 174897.87 ± 1131.98 | 175142.1 / 175883.7 / 174172.8 / 175956.9 / 173333.6 | +0.69% | -0.22% | 11,947 | 29,726 | 6,455 | 21.7 |
| n4_c0 | 3 (3) | 177172.61 | 177578.12 ± 1956.96 | 175855.6 / 179706.0 / 177172.6 | +1.86% | +0.93% | 11,872 | 484 | 199 | 41.1 |
| n32_c0 | 3 (3) | 175829.11 | 175899.20 ± 2076.19 | 178009.5 / 175829.1 / 173858.9 | +1.08% | +0.17% | 11,888 | 4,613 | 1,592 | 34.5 |

## redis

> Redis "mid": 2M random_lpush (1KB) build, then 100k random_lrange_600 measured; server in
> 1206MB (as eden), clients native. Trace = same. Metric = LRANGE req/s. No output check
> (valid = benchmark completed all requests).

metric: `lrange_rps` (higher is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh redis noprefetch 1: cgroup /sys/fs/cgroup/swappf_redis memory.max=1264582656; vm.page-cluster=3; insmod /data1/shaurya/deku_redis/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh redis default 1: cgroup /sys/fs/cgroup/swappf_redis memory.max=1264582656; vm.page-cluster=3; insmod /data1/shaurya/deku_redis/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh redis leapc 1: cgroup /sys/fs/cgroup/swappf_redis memory.max=1264582656; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh redis n4_c1 1: cgroup /sys/fs/cgroup/swappf_redis memory.max=1264582656; vm.page-cluster=3; insmod /data1/shaurya/deku_redis/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh redis n4_c0 1: cgroup /sys/fs/cgroup/swappf_redis memory.max=1264582656; vm.page-cluster=3; insmod /data1/shaurya/deku_redis/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh redis n32_c1 1: cgroup /sys/fs/cgroup/swappf_redis memory.max=1264582656; vm.page-cluster=3; insmod /data1/shaurya/deku_redis/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh redis n32_c0 1: cgroup /sys/fs/cgroup/swappf_redis memory.max=1264582656; vm.page-cluster=3; insmod /data1/shaurya/deku_redis/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: server (in cgroup): cd /data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src; ./redis-server --port 6390 --save '' --appendonly no | load (native): ./redis-benchmark -p 6390 -t random_lpush -d 1024 -n 2000000 -r 100000 | measured (native): ./redis-benchmark -p 6390 -t random_lrange_600 -d 1024 -n 100000 -r 100000
```

| config | runs (valid) | lrange_rps median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 5 (5) | 9903.93 | 7268.96 ± 3682.44 | 3347.50 / 9903.93 / 10024.06 / 9944.31 / 3125.00 | +2.59% | +0.00% | 307,303 | 0 | 0 | - |
| default | 5 (5) | 9653.44 | 8426.98 ± 2774.02 | 9653.44 / 9634.84 / 9677.73 / 3464.88 / 9704.03 | +0.00% | -2.53% | 282,795 | 153,081 | 68,928 | 45.0 |
| leapc | 5 (5) | 9860.96 | 9831.07 ± 166.33 | 9854.16 / 9551.10 / 9995.00 / 9894.13 / 9860.96 | +2.15% | -0.43% | 187,051 | 467,335 | 223,503 | 47.8 |
| n4_c1 | 5 (5) | 11220.83 | 9769.44 ± 3151.87 | 10934.94 / 11251.13 / 4137.02 / 11303.27 / 11220.83 | +16.24% | +13.30% | 221,182 | 97,497 | 89,592 | 91.9 |
| n4_c0 | 5 (5) | 10595.47 | 10574.61 ± 120.06 | 10371.29 / 10582.01 / 10646.23 / 10595.47 / 10678.06 | +9.76% | +6.98% | 224,236 | 144,345 | 105,130 | 72.8 |
| n32_c1 | 5 (5) | 10785.16 | 9432.15 ± 3019.62 | 10827.20 / 10869.57 / 4032.58 / 10785.16 / 10646.23 | +11.72% | +8.90% | 200,689 | 290,107 | 167,253 | 57.7 |
| n32_c0 | 5 (5) | 10519.67 | 9205.12 ± 2961.46 | 10519.67 / 10490.98 / 10530.75 / 10576.42 / 3907.78 | +8.97% | +6.22% | 194,477 | 371,581 | 196,433 | 52.9 |

## redis_stack

> Full DiLOS-exact Redis LRANGE (as deku_redis/linux66_stack/run_ssd.sh): Redis 6.2.4 MALLOC=libc + DiLOS
> redis.conf (port 6392), populate random_lpush -d 1024 -n 20000000 -r 100000 (~22GB, not measured),
> measure random_lrange_600 -d 1024 -n 100000 -r 100000. Deku = stack + spatial trees (st_ext4):
> eden_swappf_ext4_d5_200k.ko spatial=1 stack=1 next_n=4 (trained on the scaled Linux trace).
> Metric = LRANGE phase seconds (redis-benchmark "requests completed in"). Valid = all 100000 requests completed.

metric: `lrange_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run2.sh redis_stack noprefetch 1: cgroup /sys/fs/cgroup/swappf_redis_stack memory.max=2684354560; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 ; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run2.sh redis_stack default 1: cgroup /sys/fs/cgroup/swappf_redis_stack memory.max=2684354560; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 ; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run2.sh redis_stack leapc 1: cgroup /sys/fs/cgroup/swappf_redis_stack memory.max=2684354560; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[st_ext4] /data1/shaurya/linux66_bench/run2.sh redis_stack st_ext4 1: cgroup /sys/fs/cgroup/swappf_redis_stack memory.max=2684354560; vm.page-cluster=3; insmod /data1/shaurya/deku_redis_stack/linux66b/eden_swappf_corun_st_ext4.ko next_n=4 direct_map=0 spatial=1 stack=1 next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
command: server (in cgroup, cwd /data1/shaurya/deku_redis_stack/linux66b/runs_leap/noprefetch_r1): /data1/shaurya/deku_redis/dilos_exact/redis-6.2.4/src/redis-server /data1/shaurya/deku_redis/dilos_exact/redis.conf | load (native): /data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src/redis-benchmark -p 6392 -t random_lpush -d 1024 -n 20000000 -r 100000 | measured (native): /data1/shaurya/deku_redis/dilos-artifact/tools/redis-seq/src/redis-benchmark -p 6392 -t random_lrange_600 -d 1024 -n 100000 -r 100000
```

| config | runs (valid) | lrange_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 5 (5) | 887.93 | 887.24 ± 1.82 | 887.93 / 885.21 / 888.34 / 889.28 / 885.42 | +1.91% | +0.00% | 7,021,401 | 0 | 0 | - |
| default | 5 (5) | 904.85 | 905.16 ± 1.47 | 904.85 / 904.02 / 903.55 / 906.48 / 906.88 | +0.00% | -1.87% | 6,947,518 | 853,601 | 98,287 | 11.5 |
| leapc | 5 (5) | 734.59 | 734.91 ± 1.96 | 732.42 / 737.18 / 734.59 / 736.55 / 733.83 | +23.18% | +20.87% | 3,850,677 | 11,009,850 | 3,546,214 | 32.2 |
| st_ext4 | 5 (5) | 514.90 | 514.40 ± 1.42 | 513.42 / 515.79 / 514.90 / 515.44 / 512.44 | +75.73% | +72.45% | 3,329,469 | 3,610,633 | 3,607,411 | 99.9 |

## webservice

> Webservice (AIFM port), 5M requests, 13391MB (as eden). Trace = 1M requests, seed 1 (as eden).
> Valid = checksum == native.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh webservice noprefetch 1: cgroup /sys/fs/cgroup/swappf_webservice memory.max=14041481216; vm.page-cluster=3; insmod /data1/shaurya/deku_webservice/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh webservice default 1: cgroup /sys/fs/cgroup/swappf_webservice memory.max=14041481216; vm.page-cluster=3; insmod /data1/shaurya/deku_webservice/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh webservice leapc 1: cgroup /sys/fs/cgroup/swappf_webservice memory.max=14041481216; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh webservice n4_c1 1: cgroup /sys/fs/cgroup/swappf_webservice memory.max=14041481216; vm.page-cluster=3; insmod /data1/shaurya/deku_webservice/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh webservice n4_c0 1: cgroup /sys/fs/cgroup/swappf_webservice memory.max=14041481216; vm.page-cluster=3; insmod /data1/shaurya/deku_webservice/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh webservice n32_c1 1: cgroup /sys/fs/cgroup/swappf_webservice memory.max=14041481216; vm.page-cluster=3; insmod /data1/shaurya/deku_webservice/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh webservice n32_c0 1: cgroup /sys/fs/cgroup/swappf_webservice memory.max=14041481216; vm.page-cluster=3; insmod /data1/shaurya/deku_webservice/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_webservice/linux66b/runs/noprefetch_r1) /data1/shaurya/deku_webservice/app/webservice 5000000 > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 1 (1) | 1348.81 | 1348.81 ± 0.00 | 1348.814 | +0.67% | +0.00% | 9,500,305 | 0 | 0 | - |
| default | 1 (1) | 1357.82 | 1357.82 ± 0.00 | 1357.824 | +0.00% | -0.66% | 8,801,655 | 3,399,665 | 1,651,243 | 48.6 |
| leapc | 5 (5) | 1213.57 | 1216.59 ± 11.03 | 1206.259 / 1213.570 / 1227.241 / 1206.665 / 1229.207 | +11.89% | +11.14% | 6,309,067 | 12,787,655 | 6,805,729 | 53.2 |
| n4_c1 | 1 (1) | 975.06 | 975.06 ± 0.00 | 975.0606 | +39.26% | +38.33% | 6,328,673 | 2,846,390 | 2,836,740 | 99.7 |
| n4_c0 | 1 (1) | 996.06 | 996.06 ± 0.00 | 996.0602 | +36.32% | +35.41% | 6,550,419 | 3,134,163 | 2,987,547 | 95.3 |
| n32_c1 | 1 (1) | 1189.72 | 1189.72 ± 0.00 | 1189.724 | +14.13% | +13.37% | 7,225,840 | 8,160,817 | 4,072,013 | 49.9 |
| n32_c0 | 1 (1) | 1247.31 | 1247.31 ± 0.00 | 1247.313 | +8.86% | +8.14% | 7,711,948 | 8,163,375 | 3,990,571 | 48.9 |

## wiredtiger

> WiredTiger YCSB-E, 10M records / 10M ops, 3668MB (as eden). Trace = eden training input
> (8M records, 5M ops, cache 32, seed 7) at 3150MB. Valid = checksum == native.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh wiredtiger noprefetch 1: cgroup /sys/fs/cgroup/swappf_wiredtiger memory.max=3846176768; vm.page-cluster=3; insmod /data1/shaurya/deku_wiredtiger/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh wiredtiger default 1: cgroup /sys/fs/cgroup/swappf_wiredtiger memory.max=3846176768; vm.page-cluster=3; insmod /data1/shaurya/deku_wiredtiger/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh wiredtiger leapc 1: cgroup /sys/fs/cgroup/swappf_wiredtiger memory.max=3846176768; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh wiredtiger n4_c1 1: cgroup /sys/fs/cgroup/swappf_wiredtiger memory.max=3846176768; vm.page-cluster=3; insmod /data1/shaurya/deku_wiredtiger/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh wiredtiger n4_c0 1: cgroup /sys/fs/cgroup/swappf_wiredtiger memory.max=3846176768; vm.page-cluster=3; insmod /data1/shaurya/deku_wiredtiger/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh wiredtiger n32_c1 1: cgroup /sys/fs/cgroup/swappf_wiredtiger memory.max=3846176768; vm.page-cluster=3; insmod /data1/shaurya/deku_wiredtiger/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh wiredtiger n32_c0 1: cgroup /sys/fs/cgroup/swappf_wiredtiger memory.max=3846176768; vm.page-cluster=3; insmod /data1/shaurya/deku_wiredtiger/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_wiredtiger/linux66b/runs/noprefetch_r1) /data1/shaurya/deku_wiredtiger/app/ycsb_e 10000000 10000000 > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 5 (5) | 201.46 | 224.26 ± 43.32 | 295.5988 / 234.6736 / 189.6946 / 201.4607 / 199.8855 | +8.92% | +0.00% | 2,453,017 | 10 | 138 | 1378.0 |
| default | 5 (5) | 219.42 | 211.56 ± 15.55 | 186.3255 / 206.8366 / 223.7668 / 219.4221 / 221.4238 | +0.00% | -8.19% | 1,763,694 | 1,228,084 | 923,933 | 75.2 |
| leapc | 5 (5) | 196.32 | 205.66 ± 17.06 | 192.5170 / 196.3175 / 217.2591 / 229.9516 / 192.2373 | +11.77% | +2.62% | 1,272,112 | 1,632,721 | 1,413,080 | 86.5 |
| n4_c1 | 5 (5) | 199.10 | 199.75 ± 2.77 | 197.6978 / 199.1000 / 196.7921 / 201.8380 / 203.3464 | +10.21% | +1.19% | 1,737,712 | 570,568 | 548,131 | 96.1 |
| n4_c0 | 5 (5) | 195.45 | 200.02 ± 11.34 | 193.7696 / 195.6211 / 195.4459 / 220.2586 / 195.0059 | +12.27% | +3.08% | 1,617,940 | 640,950 | 617,590 | 96.4 |
| n32_c1 | 5 (5) | 197.43 | 203.95 ± 24.85 | 201.6050 / 187.6357 / 197.4347 / 246.8643 / 186.1930 | +11.14% | +2.04% | 1,715,439 | 640,852 | 546,900 | 85.3 |
| n32_c0 | 5 (5) | 190.87 | 194.22 ± 11.55 | 213.2682 / 195.9404 / 187.1026 / 190.8730 / 183.9180 | +14.96% | +5.55% | 1,378,911 | 1,022,126 | 928,801 | 90.9 |

## wt_c

> WiredTiger YCSB-C (100% Zipfian point reads; app/ycsb_c.c, same load as ycsb_e), in-memory WT,
> test 10M records / 20M reads. Trace = different input (8M records, 10M reads, cache 32, seed 7).
> Memory = 50% of native peak: test 3667M (7.33GB), trace 2777M (5.42GB). Valid = checksum == native.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh wt_c noprefetch 1: cgroup /sys/fs/cgroup/swappf_wt_c memory.max=3845128192; vm.page-cluster=3; insmod /data1/shaurya/deku_wt_c/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh wt_c default 1: cgroup /sys/fs/cgroup/swappf_wt_c memory.max=3845128192; vm.page-cluster=3; insmod /data1/shaurya/deku_wt_c/linux66b/eden_swappf_n32_c0.ko next_n=4 direct_map=0; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh wt_c leapc 1: cgroup /sys/fs/cgroup/swappf_wt_c memory.max=3845128192; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh wt_c n4_c0 1: cgroup /sys/fs/cgroup/swappf_wt_c memory.max=3845128192; vm.page-cluster=3; insmod /data1/shaurya/deku_wt_c/linux66b/eden_swappf_n4_c0.ko next_n=4 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh wt_c n32_c0 1: cgroup /sys/fs/cgroup/swappf_wt_c memory.max=3845128192; vm.page-cluster=3; insmod /data1/shaurya/deku_wt_c/linux66b/eden_swappf_n32_c0.ko next_n=32 direct_map=0; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_wt_c/linux66b/runs/noprefetch_r1) /data1/shaurya/deku_wiredtiger/app/ycsb_c 10000000 20000000 > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 131.95 | 135.39 ± 7.85 | 131.9468 / 129.8461 / 144.3774 | +18.58% | +0.00% | 1,680,486 | 0 | 117 | - |
| default | 3 (3) | 156.46 | 154.67 ± 10.21 | 163.8628 / 143.6748 / 156.4603 | +0.00% | -15.67% | 1,573,365 | 1,186,378 | 871,195 | 73.4 |
| leapc | 5 (5) | 130.18 | 132.07 ± 3.77 | 138.3474 / 130.1790 / 132.7598 / 129.1411 / 129.9003 | +20.19% | +1.36% | 931,998 | 1,201,067 | 1,042,835 | 86.8 |
| n4_c0 | 3 (3) | 139.73 | 139.34 ± 2.96 | 142.0907 / 139.7285 / 136.2047 | +11.97% | -5.57% | 1,371,792 | 573,407 | 555,674 | 96.9 |
| n32_c0 | 3 (3) | 132.09 | 131.77 ± 1.39 | 130.2446 / 132.9760 / 132.0917 | +18.45% | -0.11% | 1,025,083 | 750,523 | 669,259 | 89.2 |

## xalan

> SPEC 523.xalancbmk_r, refrate input, 240MB (as eden). Trace = same input. Valid = specdiff vs ref-t5.out.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh xalan noprefetch 1: cgroup /sys/fs/cgroup/swappf_xalan memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_leela/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh xalan default 1: cgroup /sys/fs/cgroup/swappf_xalan memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_leela/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh xalan leapc 1: cgroup /sys/fs/cgroup/swappf_xalan memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c1] /data1/shaurya/linux66_bench/run.sh xalan n4_c1 1: cgroup /sys/fs/cgroup/swappf_xalan memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_leela/linux66b/eden_swappf_n4_c1.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh xalan n4_c0 1: cgroup /sys/fs/cgroup/swappf_xalan memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_leela/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c1] /data1/shaurya/linux66_bench/run.sh xalan n32_c1 1: cgroup /sys/fs/cgroup/swappf_xalan memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_leela/linux66b/eden_swappf_n32_c1.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh xalan n32_c0 1: cgroup /sys/fs/cgroup/swappf_xalan memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_leela/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_xalan_leela/linux66b/runs/noprefetch_r1) /data1/shaurya/spec2017/xalan_build/cpuxalan_r -v t5.xml xalanc.xsl > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 2 (2) | 265.21 | 265.21 ± 18.02 | 252.4673 / 277.9548 | +1.12% | +0.00% | 368,189 | 4 | 1 | 28.6 |
| default | 2 (1) | 268.18 | 268.18 ± 0.00 | 268.1754 / 116.5525 | +0.00% | -1.11% | 280,865 | 128,105 | 95,873 | 74.8 |
| leapc | 5 (5) | 258.51 | 258.83 ± 4.83 | 252.4490 / 258.5112 / 265.8767 / 257.4554 / 259.8760 | +3.74% | +2.59% | 239,662 | 570,436 | 310,535 | 54.4 |
| n4_c1 | 1 (1) | 253.75 | 253.75 ± 0.00 | 253.7535 | +5.68% | +4.52% | 307,126 | 122,160 | 92,072 | 75.4 |
| n4_c0 | 1 (1) | 261.10 | 261.10 ± 0.00 | 261.1037 | +2.71% | +1.57% | 274,419 | 164,253 | 124,513 | 75.8 |
| n32_c1 | 1 (1) | 244.44 | 244.44 ± 0.00 | 244.4397 | +9.71% | +8.50% | 296,598 | 211,099 | 135,997 | 64.4 |
| n32_c0 | 1 (1) | 251.97 | 251.97 ± 0.00 | 251.9697 | +6.43% | +5.26% | 209,882 | 493,830 | 300,054 | 60.8 |

## xalan_ref

> SPEC 523.xalancbmk_r, refrate input (t5.xml), 240MB = 50% of native peak (480MB).
> Trees = xalan_train (train-input) trees, symlinked from /data1/shaurya/deku_xalan_train/linux66b.
> Valid = specdiff vs ref-t5.out.

metric: `wall_secs` (lower is better)

**How each run was done** (round 1 shown; every run's exact settings and command are in results_all.csv columns `settings` / `command` and in its run_dir):

```
[noprefetch] /data1/shaurya/linux66_bench/run.sh xalan_ref noprefetch 1: cgroup /sys/fs/cgroup/swappf_xalan_ref memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_ref/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=2 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[default] /data1/shaurya/linux66_bench/run.sh xalan_ref default 1: cgroup /sys/fs/cgroup/swappf_xalan_ref memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_ref/linux66b/eden_swappf_n32_c0.ko next_n=4; eden_swappf mode=0 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[leapc] /data1/shaurya/linux66_bench/run.sh xalan_ref leapc 1: cgroup /sys/fs/cgroup/swappf_xalan_ref memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/linux66_bench/eden_swappf_leap.ko next_n=4 direct_map=0 leap_variant=1; eden_swappf mode=3 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only, 3 = Leap), direct_map=0 (0 = swap cache only, 1 = sync map, 2 = async map); THP never; drop_caches before and after
[n4_c0] /data1/shaurya/linux66_bench/run.sh xalan_ref n4_c0 1: cgroup /sys/fs/cgroup/swappf_xalan_ref memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_ref/linux66b/eden_swappf_n4_c0.ko next_n=4; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
[n32_c0] /data1/shaurya/linux66_bench/run.sh xalan_ref n32_c0 1: cgroup /sys/fs/cgroup/swappf_xalan_ref memory.max=251658240; vm.page-cluster=3; insmod /data1/shaurya/deku_xalan_ref/linux66b/eden_swappf_n32_c0.ko next_n=32; eden_swappf mode=1 (0 = off/kernel read-ahead, 1 = trees, 2 = faulting page only); THP never; drop_caches before and after
command: (cwd /data1/shaurya/deku_xalan_ref/linux66b/runs/noprefetch_r1) /data1/shaurya/spec2017/xalan_build/cpuxalan_r -v t5.xml xalanc.xsl > stdout.out 2> stderr.log
```

| config | runs (valid) | wall_secs median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |
|---|---|---|---|---|---|---|---|---|---|---|
| noprefetch | 3 (3) | 252.97 | 251.21 ± 3.36 | 247.3273 / 253.3203 / 252.9729 | -1.72% | +0.00% | 364,549 | 7 | 7 | 110.0 |
| default | 3 (3) | 248.63 | 246.56 ± 4.57 | 241.3167 / 248.6285 / 249.7298 | +0.00% | +1.75% | 283,260 | 133,719 | 100,693 | 75.3 |
| leapc | 5 (5) | 257.38 | 257.93 ± 5.00 | 265.5417 / 251.5242 / 257.1181 / 257.3774 / 258.0777 | -3.40% | -1.71% | 240,225 | 568,056 | 307,676 | 54.2 |
| n4_c0 | 3 (3) | 248.01 | 249.32 ± 2.38 | 247.8786 / 252.0636 / 248.0073 | +0.25% | +2.00% | 285,239 | 120,928 | 90,669 | 75.0 |
| n32_c0 | 3 (3) | 249.67 | 249.30 ± 4.58 | 244.5496 / 253.6842 / 249.6688 | -0.42% | +1.32% | 212,496 | 368,229 | 213,988 | 58.1 |

