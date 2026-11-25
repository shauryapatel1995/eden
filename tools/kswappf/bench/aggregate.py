#!/usr/bin/env python3
"""Collect every workload's runs/results.csv into results_all.csv (one row per run, with
the change against Linux default read-ahead and against no prefetching) and RESULTS.md
(per-workload mean +- std per config)."""
import csv, glob, os, statistics as st
B = "/data1/shaurya/linux66_bench"
ORDER = ["noprefetch", "default", "n4_c1", "n4_c0", "n32_c1", "n32_c0"]
DESC = {"noprefetch": "no prefetching (faulting page only)",
        "default": "Linux default read-ahead (page-cluster 3)",
        "n4_c1": "trees, N=4, claim rule on", "n4_c0": "trees, N=4, claim rule off",
        "n32_c1": "trees, N=32, claim rule on", "n32_c0": "trees, N=32, claim rule off"}
wls = {}; hdr = {}; rd_ = {}
for f in sorted(glob.glob(f"{B}/wl/*.sh")):
    wl = os.path.basename(f)[:-3]
    d = [l.split("=")[1].split(";")[0] for l in open(f) if l.startswith("WL_DIR=")]
    if not d:
        continue    # shared helper sourced by workload files (e.g. tao_common.sh), not a workload
    d = d[0]
    p = f"{d}/linux66b/runs/results.csv"
    if os.path.exists(p):
        wls[wl] = list(csv.DictReader(open(p)))
        hdr[wl] = [l[2:].rstrip() for l in open(f) if l.startswith("# ")]
        for r in wls[wl]:
            rd = f"{d}/linux66b/runs/{r['config']}_r{r['round']}"
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
      "Configs: " + "; ".join(f"`{k}` = {v}" for k, v in DESC.items()), ""]
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
