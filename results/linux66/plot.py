#!/usr/bin/env python3
"""Bar plots for the Linux 6.6 swap-prefetcher campaign (results_all.csv).

fig_faults_prefetches.png: major faults and prefetched pages per config, both
    normalised to the no-prefetch major faults of the same workload.
fig_performance.png: end-to-end performance normalised to the Linux default,
    oriented so >1 is better (default/config for times, config/default for rates).

Bars are the median over rounds; error bars span the 2nd..4th of the 5 sorted
rounds (inter-quartile); workloads with one round have no error bar.
"""
import csv, os, sys
from collections import defaultdict
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "results_all.csv")
WORKLOADS = ["redis", "memcached", "wiredtiger", "webservice", "dataframe", "neo4j"]
CONFIGS = ["default", "n4_c1", "n4_c0", "n32_c1", "n32_c0"]
LABELS = {"default": "Linux default", "n4_c1": "Trees N=4, claim",
          "n4_c0": "Trees N=4, no claim", "n32_c1": "Trees N=32, claim",
          "n32_c0": "Trees N=32, no claim"}
COLORS = {"default": "#52514e", "n4_c1": "#2a78d6", "n4_c0": "#1baf7a",
          "n32_c1": "#eb6834", "n32_c0": "#6250d6"}
INK, INK2, GRID = "#0b0b0b", "#52514e", "#e4e3df"

rows = defaultdict(list)
for r in csv.DictReader(open(CSV)):
    if r["valid"] == "pass" and r["workload"] in WORKLOADS:
        rows[r["workload"], r["config"]].append(r)

def stat(vals):
    """median and (low, high) error-bar lengths over the inter-quartile rounds"""
    v = np.sort(np.asarray(vals, float))
    med = float(np.median(v))
    if len(v) < 3:
        return med, (0.0, 0.0)
    lo, hi = np.percentile(v, [25, 75])
    return med, (med - lo, hi - med)

def xlabel(wl):
    n = len(rows[wl, "default"])
    return wl if n > 1 else f"{wl}\n(1 run)"

def style(ax):
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(INK2)
    ax.tick_params(colors=INK2)
    ax.yaxis.grid(True, color=GRID, lw=0.8)
    ax.set_axisbelow(True)

plt.rcParams.update({"font.size": 10, "text.color": INK, "axes.labelcolor": INK})
EB = dict(ecolor=INK, elinewidth=1, capsize=2)

# ---- figure 1: major faults and prefetched pages vs no prefetching ----
fig, ax = plt.subplots(figsize=(14, 5))
w, gap = 0.08, 0.04
for i, wl in enumerate(WORKLOADS):
    base = np.median([float(r["majflt"]) for r in rows[wl, "noprefetch"]])
    for j, kind in enumerate(("majflt", "prefetched_pages")):
        for k, c in enumerate(CONFIGS):
            med, err = stat([float(r[kind]) / base for r in rows[wl, c]])
            x = i + (j * len(CONFIGS) + k) * w + j * gap - (2 * len(CONFIGS) * w + gap) / 2 + w / 2
            ax.bar(x, med, w * 0.9, color=COLORS[c], edgecolor=COLORS[c],
                   hatch="////" if j else None, fill=not j, lw=1.2,
                   yerr=np.array([err]).T, error_kw=EB)
ax.axhline(1, color=INK2, lw=1, ls="--")
ax.text(len(WORKLOADS) - 0.45, 1.02, "no prefetching", color=INK2, ha="right", va="bottom", fontsize=9)
ax.set_xticks(range(len(WORKLOADS)), [xlabel(wl) for wl in WORKLOADS])
ax.set_ylabel("Pages / no-prefetch major faults")
ax.set_title("Major faults (solid) and prefetched pages (hatched), normalised to no prefetching",
             loc="left", fontsize=11)
style(ax)
handles = [plt.Rectangle((0, 0), 1, 1, color=COLORS[c]) for c in CONFIGS]
handles += [plt.Rectangle((0, 0), 1, 1, fc=INK2, ec=INK2),
            plt.Rectangle((0, 0), 1, 1, fill=False, ec=INK2, hatch="////")]
ax.legend(handles, [LABELS[c] for c in CONFIGS] + ["major faults", "prefetched pages"],
          ncol=7, frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.12), fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(HERE, "fig_faults_prefetches.png"), dpi=200)

# ---- figure 2: performance normalised to Linux default, >1 = better ----
fig, ax = plt.subplots(figsize=(12, 4.5))
w = 0.15
for i, wl in enumerate(WORKLOADS):
    base = np.median([float(r["metric"]) for r in rows[wl, "default"]])
    lower = rows[wl, "default"][0]["better"] == "lower"
    for k, c in enumerate(CONFIGS):
        vals = [base / float(r["metric"]) if lower else float(r["metric"]) / base
                for r in rows[wl, c]]
        med, err = stat(vals)
        x = i + (k - (len(CONFIGS) - 1) / 2) * w
        ax.bar(x, med, w * 0.88, color=COLORS[c], yerr=np.array([err]).T, error_kw=EB)
ax.axhline(1, color=INK2, lw=1, ls="--")
ax.set_xticks(range(len(WORKLOADS)),
              [f"{xlabel(wl)}\n({rows[wl, 'default'][0]['metric_name']})" for wl in WORKLOADS])
ax.set_ylim(0.6, None)
ax.set_ylabel("Performance / Linux default (>1 is better)")
ax.set_title("End-to-end performance normalised to the Linux default prefetcher", loc="left", fontsize=11)
style(ax)
ax.legend([plt.Rectangle((0, 0), 1, 1, color=COLORS[c]) for c in CONFIGS],
          [LABELS[c] for c in CONFIGS], ncol=5, frameon=False, loc="upper center",
          bbox_to_anchor=(0.5, -0.2), fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(HERE, "fig_performance.png"), dpi=200)
