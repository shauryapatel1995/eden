#!/usr/bin/env python3
"""Collect every workload's runs/results.csv into results_all.csv (one row per run, with
the change against Linux default read-ahead and against no prefetching) and RESULTS.md
(per-workload mean +- std per config)."""
import csv, glob, os, statistics as st
B = "/data1/shaurya/linux66_bench"
ORDER = ["noprefetch", "default", "leapc", "n4_c1", "n4_c0", "n32_c1", "n32_c0", "st_ext4"]
EXCLUDED = ("neo4j", "tao", "xgb_py")       # not reported (user decision)
BEST = dict(x.split(":") for x in ("cactu:n32_c0 cactu_ref:n32_c0 dataframe:n32_c0 etc:n4_c0 gap_pr:n32_c0 "
            "gap_sssp:n32_c0 lbm_ref:n32_c0 mcf:n32_c0 mcf_ref:n32_c0 memcached:n32_c0 memtier:n4_c0 redis:n4_c0 "
            "webservice:n4_c0 wiredtiger:n32_c0 wt_c:n32_c0 xalan:n4_c0 xalan_ref:n4_c0 redis_stack:st_ext4").split())
DESC = {"noprefetch": "no prefetching (faulting page only)",
        "default": "Linux default read-ahead (page-cluster 3)",
        "n4_c1": "trees, N=4, claim rule on", "n4_c0": "trees, N=4, claim rule off",
        "n32_c1": "trees, N=32, claim rule on", "n32_c0": "trees, N=32, claim rule off",
        "leapc": "Leap (ATC'20) in the same module (mode 3): majority-trend detection over the last 32 faults, "
                 "released-code window (hits+2, min 4, max 8 pages), aligned-block fallback without a trend",
        "st_ext4": "stack + spatial trees (Redis LRANGE stack test)"}
wls = {}; hdr = {}; rd_ = {}; seen_dirs = set()
for f in sorted(glob.glob(f"{B}/wl/*.sh")):
    wl = os.path.basename(f)[:-3]
    d = [l.split("=")[1].split(";")[0] for l in open(f) if l.startswith("WL_DIR=")]
    if not d:
        continue    # shared helper sourced by workload files (e.g. tao_common.sh), not a workload
    d = d[0]
    if wl.startswith(EXCLUDED) or d in seen_dirs:
        continue                            # excluded, or a variant sharing another workload's directory
    seen_dirs.add(d)
    p = f"{d}/linux66b/runs/results.csv"
    pl = f"{d}/linux66b/runs_leap/results.csv"
    rows = list(csv.DictReader(open(p))) if os.path.exists(p) else []
    for r in (csv.DictReader(open(pl)) if os.path.exists(pl) else []):
        # Leap baseline runs (leapc); redis_stack was run only in runs_leap (all its configs). The other
        # runs_leap rows are same-kernel re-runs (WT, wt_c) kept out of this table.
        if r["config"] == "leapc" or wl == "redis_stack":
            r["_dir"] = "runs_leap"; rows.append(r)
    if rows:
        wls[wl] = rows
        hdr[wl] = [l[2:].rstrip() for l in open(f) if l.startswith("# ")]
        for r in wls[wl]:
            rd = f"{d}/linux66b/{r.get('_dir', 'runs')}/{r['config']}_r{r['round']}"
            rd_[id(r)] = rd
            r["run_dir"] = rd
            r["settings"] = open(f"{rd}/settings").read().strip() if os.path.exists(f"{rd}/settings") else ""
            r["command"] = " | ".join(open(f"{rd}/cmd").read().split("\n")).strip(" |") if os.path.exists(f"{rd}/cmd") else ""
def base_of(rows, cfg):
    """baseline = median of the config's valid runs (robust to the occasional slow-mode run)"""
    v = [float(r["metric"]) for r in rows if r["config"] == cfg and r["valid"] != "FAIL" and r["metric"]]
    return st.median(v) if v else None
def change(x, base, better):
    """(raw % change of the metric, improvement % where positive = better)"""
    if base is None or not x: return "", ""
    raw = (x - base) / base * 100
    imp = (base / x - 1) * 100 if better == "lower" else (x / base - 1) * 100
    return f"{raw:+.2f}", f"{imp:+.2f}"
cols = ["workload", "config", "round", "metric_name", "metric", "better",
        "metric_change_vs_default_pct", "improvement_vs_default_pct",
        "metric_change_vs_noprefetch_pct", "improvement_vs_noprefetch_pct",
        "majflt", "prefetched_pages", "prefetch_pages_used", "prefetch_used_pct",
        "pswpin", "mod_faults", "mod_predicted", "valid", "run_dir", "settings", "command"]
out = []
md = ["# Linux 6.6 swap prefetcher: all workloads", "",
      "One run at a time over SSD swap (/data1/swapfile_swappf), THP never, caches dropped before and after every run, "
      "app killed after each run, cgroup v2 memory.max = the eden local memory.", "",
      "Columns: **metric** = end-to-end metric (lower or higher is better, see the header), "
      "**majflt** = major page faults of the workload's cgroup in the measured phase, "
      "**prefetched** = pages read ahead (vmstat swap_ra; kernel read-ahead or the module), "
      "**used** = of those, pages later hit in the swap cache (swap_ra_hit). "
      "**vs default / vs noprefetch** = improvement of the config's median over the baseline's median, positive = better "
      "(time: base/x - 1; throughput: x/base - 1); in results_all.csv the same per run against the baseline median. "
      "Medians because some workloads (Redis) have an occasional slow-mode run in every config. "
      "Invalid runs are excluded.", "",
      "Not run: omnetpp (104M swap-ins at its eden size, 111MB; about an hour per run on the SSD - "
      "skipped at the user's request; trace and trees are in /data1/shaurya/deku_omnetpp/linux66b). "
      "ll, adult, go and leela had no complete eden benchmark.", "",
      "Rounds: 5 per config, except webservice and dataframe (1 run each, at the user's request - "
      "about 22 min per webservice run). Single-run medians are just that run.", "",
      "SPEC: `mcf`, `cactu` and `xalan` are trained and tested on the same input (mcf/cactu train, "
      "xalan refrate) and are kept only for reference. The real SPEC results are `mcf_ref`, `cactu_ref` "
      "and `xalan_ref`: refrate input, trees trained only on the train-input trace "
      "(mcf/cactu trees from the train trace; xalan trees from `xalan_train`).", "",
      "Configs: " + "; ".join(f"`{k}` = {v}" for k, v in DESC.items()), "",
      "Leap rows (`leapc`) were run on the current kernel build (Nov 25-26, 2026); the other configs come from the "
      "October campaign (earlier builds of the same kernel). Leap's paper window (`leap`, shrinks to 0-1 pages without "
      "hits) was 28-50% slower than read-ahead and is not reported.", "",
      "## Summary: medians, Deku (best tree config) vs Leap and read-ahead", "",
      "| workload | metric | read-ahead | Leap | Deku (config) | Deku vs Leap | Deku vs read-ahead | Leap vs read-ahead |",
      "|---|---|---|---|---|---|---|---|"]
def _med(rows, cfg):
    v = [float(r["metric"]) for r in rows if r["config"] == cfg and r["valid"] != "FAIL" and r["metric"]]
    return (st.median(v), len(v)) if v else (None, 0)
def _sp(a, b, better):
    return "-" if not a or not b else f"{(b / a if better == 'lower' else a / b):.3f}x"
for wl, rows in wls.items():
    better = rows[0]["better"]; cfg = BEST.get(wl)
    (d_, nd), (l_, nl), (t_, nt) = _med(rows, "default"), _med(rows, "leapc"), _med(rows, cfg)
    f = lambda m, n: "-" if m is None else f"{m:,.2f}" + ("" if n > 1 else "*")
    md.append(f"| {wl} | {rows[0]['metric_name']} ({better}) | {f(d_, nd)} | {f(l_, nl)} | {f(t_, nt)} ({cfg}) | "
              f"{_sp(t_, l_, better)} | {_sp(t_, d_, better)} | {_sp(l_, d_, better)} |")
md += ["", "x = speedup (time: base/x; throughput: x/base); > 1 means the first-named config is better. "
       "\\* = single run. `cactu`, `mcf`, `xalan` test on their training input (reference only).", ""]
for wl, rows in wls.items():
    if not rows: continue
    better = rows[0]["better"]; name = rows[0]["metric_name"]
    base_d, base_n = base_of(rows, "default"), base_of(rows, "noprefetch")
    for r in rows:
        x = float(r["metric"]) if r["metric"] else None
        cd, id_ = change(x, base_d, better); cn, in_ = change(x, base_n, better)
        pf, used = int(r["prefetched_pages"]), int(r["prefetch_pages_used"])
        out.append({**{k: r[k] for k in ["workload", "config", "round", "metric_name", "metric", "better",
                                         "majflt", "prefetched_pages", "prefetch_pages_used", "pswpin",
                                         "mod_faults", "mod_predicted", "valid", "run_dir", "settings", "command"]},
                    "metric_change_vs_default_pct": cd, "improvement_vs_default_pct": id_,
                    "metric_change_vs_noprefetch_pct": cn, "improvement_vs_noprefetch_pct": in_,
                    "prefetch_used_pct": f"{100*used/pf:.1f}" if pf else ""})
    ex = {c: next((r for r in rows if r["config"] == c), None) for c in ORDER}
    md += [f"## {wl}", "", *[f"> {h}" for h in hdr[wl]], "", f"metric: `{name}` ({better} is better)", "",
           "**How each run was done** (round 1 shown; every run's exact settings and command are in "
           "results_all.csv columns `settings` / `command` and in its run_dir):", "", "```"]
    for c in ORDER:
        if ex[c]: md += [f"[{c}] {ex[c]['settings']}"]
    first = next(r for r in rows)
    md += [f"command: {first['command']}", "```", "",
           f"| config | runs (valid) | {name} median | mean ± std | per-run values | vs default | vs noprefetch | majflt | prefetched | used | used % |",
           "|---|---|---|---|---|---|---|---|---|---|---|"]
    for cfg in ORDER + sorted({r["config"] for r in rows} - set(ORDER)):
        rs = [r for r in rows if r["config"] == cfg]
        if not rs: continue
        ok = [r for r in rs if r["valid"] != "FAIL" and r["metric"]]
        v = [float(r["metric"]) for r in ok]
        m = st.mean(v) if v else None; sd = st.stdev(v) if len(v) > 1 else 0
        med = st.median(v) if v else None
        avg = lambda k: st.mean(int(r[k]) for r in ok) if ok else 0
        pf, used = avg("prefetched_pages"), avg("prefetch_pages_used")
        upct = f"{100*used/pf:.1f}" if pf else "-"
        md.append(f"| {cfg} | {len(rs)} ({len(ok)}) | {med:.2f} | {m:.2f} ± {sd:.2f} | {' / '.join(r['metric'][:8] for r in rs)} | "
                  f"{change(med, base_d, better)[1] or '-'}% | {change(med, base_n, better)[1] or '-'}% | "
                  f"{avg('majflt'):,.0f} | {pf:,.0f} | {used:,.0f} | {upct} |" if m is not None else
                  f"| {cfg} | {len(rs)} (0) | - | - | - | - | - | - | - | - | - |")
    md.append("")
with open(f"{B}/results_all.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=cols); w.writeheader(); w.writerows(out)
open(f"{B}/RESULTS.md", "w").write("\n".join(md) + "\n")
print(f"{len(out)} runs from {len(wls)} workloads -> {B}/results_all.csv, {B}/RESULTS.md")
