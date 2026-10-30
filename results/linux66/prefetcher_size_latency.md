# Prefetcher size and inference latency

Claim-0 PySTreeD spatial trees (depth 3, F0.5), one tree set per workload and window N (candidates +1..+N).

- **Size**: lines of the generated `native_tree_model_spatial.h` (non-blank, non-comment) and the `.text` size of the built `eden_swappf_n<N>_c0.ko`.
  Shared code on top of every tree set: `tools/kswappf/eden_swappf.c` (108 lines) and the kernel part of `tools/fltrace/native_tree_eval.h` (28 lines).
- **Latency**: one `eden_predict()` call (history update, per-PC tree lookup, fault-level mask, then `st_predict()` for k = 1..N),
  compiled `gcc -O2` in user space from the same headers, replaying the first 10k faults of the workload's training trace on one pinned core.
  Timed per call with `rdtsc`; the median empty-timer overhead (~20 ns) is subtracted. Includes faults whose PC has no tree.
  For scale: an SSD swap-in major fault on this machine costs 22-29 us.

| Workload | N | PCs with a tree | Positive leaves | Tree nodes | Generated C lines | .ko text (B) | Faults with a tree (%) | Mean (ns) | p50 (ns) | p99 (ns) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| redis | 4 | 17 | 29 | 141 | 456 | 9173 | 96.7 | 34.7 | 33.2 | 72.6 |
| redis | 32 | 21 | 33 | 179 | 578 | 10601 | 99.2 | 77.4 | 98.2 | 161.1 |
| memcached | 4 | 3 | 7 | 31 | 129 | 4312 | 96.8 | 28.7 | 27.0 | 41.5 |
| memcached | 32 | 5 | 12 | 51 | 187 | 5171 | 99.4 | 112.4 | 111.3 | 139.6 |
| wiredtiger | 4 | 49 | 108 | 453 | 3071 | 56851 | 92.6 | 38.3 | 33.9 | 129.3 |
| wiredtiger | 32 | 68 | 110 | 546 | 3739 | 66408 | 99.0 | 70.4 | 44.2 | 197.0 |
| webservice | 4 | 4 | 6 | 32 | 114 | 3854 | 98.5 | 21.8 | 20.0 | 47.7 |
| webservice | 32 | 6 | 6 | 40 | 146 | 4058 | 99.6 | 48.7 | 49.8 | 80.2 |
| dataframe | 4 | 32 | 42 | 84 | 525 | 8920 | 99.6 | 21.7 | 21.4 | 23.5 |
| dataframe | 32 | 39 | 65 | 169 | 716 | 11476 | 99.6 | 80.6 | 77.4 | 91.3 |
| neo4j | 4 | 102 | 294 | 976 | 5070 | 92056 | 94.8 | 33.6 | 31.1 | 132.0 |
| neo4j | 32 | 389 | 1095 | 4061 | 13251 | 212824 | 97.0 | 102.7 | 91.9 | 246.1 |
| mcf_ref | 4 | 61 | 146 | 521 | 1654 | 24840 | 99.9 | 25.0 | 23.5 | 47.0 |
| mcf_ref | 32 | 70 | 158 | 606 | 1832 | 26904 | 99.9 | 124.9 | 134.1 | 149.3 |
| cactu_ref | 4 | 371 | 739 | 2497 | 8131 | 114664 | 99.1 | 29.9 | 27.0 | 139.0 |
| cactu_ref | 32 | 421 | 811 | 3039 | 9188 | 125965 | 99.4 | 139.4 | 136.2 | 298.6 |
| xalan_ref | 4 | 27 | 67 | 247 | 708 | 12457 | 99.5 | 39.5 | 35.9 | 85.7 |
| xalan_ref | 32 | 37 | 94 | 345 | 932 | 16008 | 99.5 | 166.9 | 168.7 | 247.5 |
| xgb_py | 4 | 30 | 77 | 312 | 1082 | 20604 | 87.8 | 42.4 | 42.2 | 117.5 |
| xgb_py | 32 | 76 | 190 | 812 | 2284 | 37250 | 93.3 | 166.1 | 179.1 | 308.3 |
| wt_c | 4 | 56 | 120 | 520 | 3326 | 61692 | 95.4 | 36.8 | 32.5 | 121.7 |
| wt_c | 32 | 81 | 132 | 675 | 3980 | 68885 | 99.0 | 53.4 | 32.5 | 202.6 |
| gap_pr | 4 | 10 | 15 | 26 | 195 | 5683 | 99.4 | 28.6 | 21.4 | 53.9 |
| gap_pr | 32 | 12 | 29 | 94 | 283 | 7183 | 99.4 | 128.0 | 122.4 | 181.8 |
| gap_sssp | 4 | 13 | 25 | 109 | 381 | 8911 | 99.6 | 27.9 | 21.4 | 69.8 |
| gap_sssp | 32 | 16 | 34 | 146 | 467 | 10252 | 99.6 | 101.0 | 83.6 | 200.5 |
| lbm_ref | 4 | 32 | 102 | 348 | 797 | 14955 | 99.8 | 34.1 | 31.8 | 58.1 |
| lbm_ref | 32 | 42 | 104 | 372 | 952 | 15999 | 99.8 | 97.3 | 88.5 | 167.2 |
| memtier | 4 | 1 | 1 | 7 | 46 | 3471 | 87.0 | 12.9 | 13.8 | 28.3 |
| memtier | 32 | 3 | 5 | 29 | 94 | 4250 | 97.8 | 58.1 | 65.0 | 105.1 |
| etc | 4 | 13 | 16 | 95 | 499 | 10689 | 99.0 | 25.2 | 22.8 | 54.6 |
