# linux66_bench: Linux 6.6 swap-prefetcher benchmark harness

Copy of `/data1/shaurya/linux66_bench` (scripts only; results and data stay on the machine).
Scripts use absolute paths under `/data1/shaurya`.

- `common.sh`: shared helpers (cgroup, swap hygiene, drop_caches, counter snapshots).
- `wl/<workload>.sh`: one file per workload (WL_DIR, WL_MEM, metric, `wl_run`, validation).
- `trace.sh` / `train.sh`: brd-swap trace of a workload, then label + train the PySTreeD trees and build the module.
- `run.sh <wl> <cfg> <round>`: one timed run over SSD swap. cfg: `noprefetch`, `default` (Linux read-ahead),
  `n<N>_c<0|1>` (trees), `leap` / `leapc` (Leap: paper window / released-code window).
- `run2.sh`: same for the current kernel build (current-source modules: `eden_swappf_leap.ko`, `eden_swappf_corun_<cfg>.ko`).
- `campaign.sh`, `leap_campaign.sh`, `followup_campaign.sh`: resumable multi-round campaigns; `aggregate.py` -> RESULTS.md.
- `corun.sh` / `corun_campaign.sh`: two co-running copies (multi-tenancy).
- `mpc.sh`, `mpc_campaign.sh`, `deku_mem.py`: fixed memory.max sweeps; minimum memory for <=5% / <=10% slowdown.
- `senpai_*.sh`: Senpai (TMO-style) reclamation experiments.
