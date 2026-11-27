#!/usr/bin/env python3
"""Figures for the 50%-memory campaign (results/results_all.csv -> results/*.pdf).

  fig_speedup_vs_ra.pdf   speedup over Linux read-ahead: Leap and Deku per workload + geomean
  fig_deku_vs_leap.pdf    Deku speedup over Leap per workload + geomean
  fig_precision_recall.pdf  prefetch precision and recall: read-ahead, Leap, Deku
  figures_50pct.pdf       all three, one per page

Medians over each config's valid runs; speedup = base/x for time metrics, x/base for throughput.
Deku = each workload's best tree config. Usage: plot_results.py [results_all.csv] [out_dir]
"""
import csv, math, os, statistics as st, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "results", "results_all.csv")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "results")

# test workloads, paper order: (key, label, Deku config)
WLS = [("mcf_ref", "mcf", "n32_c0"), ("cactu_ref", "cactuBSSN", "n32_c0"), ("lbm_ref", "lbm", "n32_c0"),
       ("xalan_ref", "xalancbmk", "n4_c0"), ("gap_pr", "PageRank", "n32_c0"), ("gap_sssp", "SSSP", "n32_c0"),
       ("redis", "Redis LRANGE", "n4_c0"), ("redis_stack", "Redis LRANGE-stack", "st_ext4"),
       ("memtier", "Redis USR", "n4_c0"), ("etc", "Redis ETC", "n4_c0"), ("memcached", "Memcached", "n32_c0"),
       ("wiredtiger", "WT YCSB-E", "n32_c0"), ("wt_c", "WT YCSB-C", "n32_c0"), ("dataframe", "Dataframe", "n32_c0"),
       ("webservice", "Webservice", "n4_c0")]

# reference categorical palette, slots 1-2 (validated pair); read-ahead is the neutral baseline
DEKU, LEAP, RA = "#2a78d6", "#eb6834", "#a3a29a"
INK, MUTED, GRID = "#1f1f1e", "#5f5e58", "#e4e3dd"
plt.rcParams.update({"font.size": 7, "axes.titlesize": 7.5, "axes.labelsize": 7, "xtick.labelsize": 6.2,
                     "ytick.labelsize": 6.5, "legend.fontsize": 6.5, "pdf.fonttype": 42, "ps.fonttype": 42,
                     "axes.edgecolor": MUTED, "axes.labelcolor": INK, "xtick.color": MUTED, "ytick.color": MUTED,
                     "text.color": INK, "hatch.linewidth": 0.6, "hatch.color": "white"})

rows = [r for r in csv.DictReader(open(SRC)) if r["valid"] != "FAIL" and r["metric"]]


def runs(wl, cfg):
    return [r for r in rows if r["workload"] == wl and r["config"] == cfg]


def med(wl, cfg):
    v = [float(r["metric"]) for r in runs(wl, cfg)]
    return (st.median(v), len(v)) if v else (None, 0)


def speedup(wl, cfg, base):
    (x, _), (b, _) = med(wl, cfg), med(wl, base)
    better = runs(wl, cfg)[0]["better"]
    return b / x if better == "lower" else x / b


def prec_rec(wl, cfg):
    p = [int(r["prefetch_pages_used"]) / int(r["prefetched_pages"]) for r in runs(wl, cfg) if int(r["prefetched_pages"])]
    rc = [int(r["prefetch_pages_used"]) / (int(r["prefetch_pages_used"]) + int(r["majflt"])) for r in runs(wl, cfg)]
    return st.median(p), st.median(rc)


def gmean(v):
    return math.exp(sum(map(math.log, v)) / len(v))


def single(wl, cfgs):
    return any(med(wl, c)[1] == 1 for c in cfgs)


def label(wl, lab, cfgs):
    return lab + ("*" if single(wl, cfgs) else "")


def style(ax, ylabel):
    ax.set_ylabel(ylabel)
    ax.yaxis.grid(True, color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.spines["left"].set_linewidth(0.6)
    ax.spines["bottom"].set_linewidth(0.6)
    ax.tick_params(length=2, width=0.6)


def bars(ax, xs, vals, w, color, name, hatch=None):
    # thin bars with a 2px-equivalent gap; values anchored to the y=1 baseline for speedup plots
    return ax.bar(xs, vals, w * 0.9, color=color, label=name, hatch=hatch, edgecolor="white", linewidth=0.4)


def fig_speedup_vs_ra():
    fig, ax = plt.subplots(figsize=(7.0, 2.3))
    names, leap, deku = [], [], []
    for wl, lab, cfg in WLS:
        names.append(label(wl, lab, ["default", "leapc", cfg]))
        leap.append(speedup(wl, "leapc", "default")); deku.append(speedup(wl, cfg, "default"))
    names.append("Geomean"); leap.append(gmean(leap)); deku.append(gmean(deku))
    w = 0.38; xs = range(len(names))
    bars(ax, [x - w / 2 for x in xs], leap, w, LEAP, "Leap", hatch="////")
    bars(ax, [x + w / 2 for x in xs], deku, w, DEKU, "Deku")
    ax.axhline(1.0, color=INK, linewidth=0.7)
    ax.annotate(f"{deku[-1]:.2f}x", (len(names) - 1 + w / 2, deku[-1]), xytext=(0, 1.5),
                textcoords="offset points", ha="center", va="bottom", fontsize=6)
    ax.annotate(f"{leap[-1]:.2f}x", (len(names) - 1 - w / 2, leap[-1]), xytext=(0, 1.5),
                textcoords="offset points", ha="center", va="bottom", fontsize=6)
    ax.set_xticks(list(xs)); ax.set_xticklabels(names, rotation=35, ha="right", rotation_mode="anchor")
    ax.axvline(len(names) - 1.5, color=GRID, linewidth=0.8)
    ax.set_ylim(0.6, max(max(leap), max(deku)) * 1.08); ax.set_xlim(-0.6, len(names) - 0.4)
    style(ax, "Speedup over\nLinux read-ahead")
    ax.legend(loc="upper left", ncol=2, frameon=False)
    fig.tight_layout(pad=0.3)
    return fig, dict(zip(names, zip(leap, deku)))


def fig_deku_vs_leap():
    fig, ax = plt.subplots(figsize=(7.0, 2.1))
    names, v = [], []
    for wl, lab, cfg in WLS:
        names.append(label(wl, lab, ["leapc", cfg])); v.append(speedup(wl, cfg, "leapc"))
    order = sorted(range(len(v)), key=lambda i: -v[i])
    names = [names[i] for i in order] + ["Geomean"]; v = [v[i] for i in order] + [gmean(v)]
    xs = range(len(names))
    ax.bar(xs, [x - 1 for x in v], 0.55, bottom=1, color=[DEKU] * (len(v) - 1) + ["#1c5cab"], edgecolor="white",
           linewidth=0.4)
    ax.axhline(1.0, color=INK, linewidth=0.7)
    for i in (0, len(v) - 1):
        ax.annotate(f"{v[i]:.2f}x", (i, v[i]), xytext=(0, 1.5), textcoords="offset points", ha="center",
                    va="bottom", fontsize=6)
    ax.set_xticks(list(xs)); ax.set_xticklabels(names, rotation=35, ha="right", rotation_mode="anchor")
    ax.axvline(len(names) - 1.5, color=GRID, linewidth=0.8)
    ax.set_ylim(0.9, max(v) * 1.06); ax.set_xlim(-0.6, len(names) - 0.4)
    style(ax, "Deku speedup\nover Leap")
    fig.tight_layout(pad=0.3)
    return fig, dict(zip(names, v))


def fig_precision_recall():
    fig, axs = plt.subplots(2, 1, figsize=(7.0, 3.6), sharex=True)
    names = [label(wl, lab, ["default", "leapc", cfg]) for wl, lab, cfg in WLS]
    data = {c: [] for c in ("ra", "leap", "deku")}
    for wl, lab, cfg in WLS:
        for key, c in (("ra", "default"), ("leap", "leapc"), ("deku", cfg)):
            data[key].append(prec_rec(wl, c))
    w = 0.27; xs = list(range(len(names)))
    for k, (ax, title) in enumerate(zip(axs, ("Precision (used / prefetched)", "Recall (used / (used + major faults))"))):
        bars(ax, [x - w for x in xs], [p[k] for p in data["ra"]], w, RA, "Read-ahead")
        bars(ax, xs, [p[k] for p in data["leap"]], w, LEAP, "Leap", hatch="////")
        bars(ax, [x + w for x in xs], [p[k] for p in data["deku"]], w, DEKU, "Deku")
        ax.set_ylim(0, 1.0); ax.set_xlim(-0.6, len(names) - 0.4)
        style(ax, title.split(" (")[0]); ax.set_title(title, loc="left", color=MUTED, fontsize=6.5, pad=2)
    axs[0].legend(loc="upper center", ncol=3, frameon=False, bbox_to_anchor=(0.5, 1.28))
    axs[1].set_xticks(xs); axs[1].set_xticklabels(names, rotation=35, ha="right", rotation_mode="anchor")
    fig.tight_layout(pad=0.3, h_pad=0.6)
    return fig, data


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    figs = [("fig_speedup_vs_ra", fig_speedup_vs_ra), ("fig_deku_vs_leap", fig_deku_vs_leap),
            ("fig_precision_recall", fig_precision_recall)]
    with PdfPages(os.path.join(OUT, "figures_50pct.pdf")) as pdf:
        for name, f in figs:
            fig, d = f()
            fig.savefig(os.path.join(OUT, name + ".pdf"))
            pdf.savefig(fig)
            if os.environ.get("PNG"):
                fig.savefig(os.path.join(os.environ["PNG"], name + ".png"), dpi=200)
            print(name, {k: (tuple(round(x, 3) for x in v) if isinstance(v, tuple) else round(v, 3))
                         for k, v in (d.items() if name != "fig_precision_recall" else [])})
            plt.close(fig)
    print("->", OUT)
