#!/usr/bin/env python3
"""Figures of the RCS paper from a grid's summary.csv (tools/rcs/grid.py --summary).

    tools/rcs/plots.py results/rcs/summary.csv --out docs/figures/rcs

fig2-6_<metric>.png   RCS against the load for every J, mu = 0 and mu = 1 side by side
                      (the paper's Figs 2 to 6: PRR, Pcoll, Pdrop, E[PIR], E[AoI])
fig7_comparison.png   RCS J = 1 and J = 36 against SPS (Ppers 0, 0.8) and DS, mu = 1
fig8_aoi_violation.png the same schemes, V(Ath) for Ath = 100, 200, 500 ms
highway_<metric>.png  the highway: RCS with each tone setting against SPS and DS
"""
import argparse
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pandas as pd  # noqa: E402

# Okabe-Ito, readable with the common colour-vision deficiencies
COLOURS = ["#0072B2", "#E69F00", "#009E73", "#CC79A7", "#56B4E9", "#D55E00", "#F0E442", "#000000"]
MARKERS = "osD^v<>p"
SCHEMES = [("rcs", 1, "RCS J=1"), ("rcs", 36, "RCS J=36"), ("sps0", None, "SPS Ppers=0"),
           ("sps08", None, "SPS Ppers=0.8"), ("ds", None, "DS")]
IDEAL_METRICS = [("prr", "PRR", False), ("pcoll", "Pcoll", True), ("pdrop", "Pdrop", True),
                 ("pir_ms", "E[PIR] (ms)", False), ("aoi_ms", "E[AoI] (ms)", False)]


def curve(frame, **match):
    sub = frame
    for key, value in match.items():
        sub = sub[sub[key].isna()] if value is None else sub[sub[key] == value]
    return sub.groupby("load").mean(numeric_only=True).sort_index()


def j_sweep(frame, out):
    ideal = frame[(frame.model == "ideal") & (frame.scheme == "rcs")]
    for i, (metric, label, log) in enumerate(IDEAL_METRICS):
        fig, axes = plt.subplots(1, 2, figsize=(10, 3.6), sharey=True)
        for ax, mu in zip(axes, (0, 1)):
            for k, j in enumerate(sorted(ideal.groups.dropna().unique())):
                c = curve(ideal, mu=mu, groups=j)
                if len(c):
                    ax.plot(c.index, c[metric], marker=MARKERS[k % 8], color=COLOURS[k % 8], label="J=%d" % j)
            ax.set_title("mu = %d" % mu)
            ax.set_xlabel("load a")
            ax.grid(alpha=0.3)
            if log:
                ax.set_yscale("log")
        axes[0].set_ylabel(label)
        axes[1].legend(ncol=2, fontsize=8)
        fig.suptitle("Fig. %d: %s, ideal model" % (i + 2, label))
        fig.tight_layout()
        fig.savefig(os.path.join(out, "fig%d_%s.png" % (i + 2, metric)), dpi=150)
        plt.close(fig)


def comparison(frame, out, model="ideal", suffix=""):
    sub = frame[(frame.model == model) & (frame.mu == 1)]
    if model == "highway":
        sub = sub[(sub.scheme != "rcs") | (sub.tone == "all")]
    metrics = [("prr" + suffix, "PRR"), ("pir_ms" + suffix, "E[PIR] (ms)"), ("aoi_ms" + suffix, "E[AoI] (ms)")]
    fig, axes = plt.subplots(1, 3, figsize=(14, 3.8))
    for ax, (metric, label) in zip(axes, metrics):
        for k, (scheme, j, name) in enumerate(SCHEMES):
            c = curve(sub, scheme=scheme, groups=j)
            if len(c) and metric in c:
                ax.plot(c.index, c[metric], marker=MARKERS[k], color=COLOURS[k], label=name)
        ax.set_xlabel("load a")
        ax.set_ylabel(label)
        ax.grid(alpha=0.3)
    axes[2].set_yscale("log")
    axes[0].legend(fontsize=8)
    fig.suptitle("Fig. 7: RCS against SPS and DS, mu = 1, %s%s" % (model, " " + suffix.strip("_") if suffix else ""))
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig7_comparison%s.png" % ("" if model == "ideal" else "_" + model + suffix)), dpi=150)
    plt.close(fig)

    fig, axes = plt.subplots(1, 3, figsize=(14, 3.8), sharey=True)
    for ax, threshold in zip(axes, (100, 200, 500)):
        metric = "aoi_violation_%d%s" % (threshold, suffix)
        for k, (scheme, j, name) in enumerate(SCHEMES):
            c = curve(sub, scheme=scheme, groups=j)
            if len(c) and metric in c:
                ax.plot(c.index, c[metric], marker=MARKERS[k], color=COLOURS[k], label=name)
        ax.set_title("Ath = %d ms" % threshold)
        ax.set_xlabel("load a")
        ax.set_yscale("log")
        ax.grid(alpha=0.3)
    axes[0].set_ylabel("AoI violation probability")
    axes[0].legend(fontsize=8)
    fig.suptitle("Fig. 8: AoI violation, mu = 1, %s%s" % (model, " " + suffix.strip("_") if suffix else ""))
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fig8_aoi_violation%s.png" % ("" if model == "ideal" else "_" + model + suffix)), dpi=150)
    plt.close(fig)


def highway_tones(frame, out, distance=300):
    sub = frame[frame.model == "highway"]
    if sub.empty:
        return
    metrics = [("prr_%dm" % distance, "PRR within %d m" % distance),
               ("pir_ms_%dm" % distance, "E[PIR] within %d m (ms)" % distance),
               ("aoi_ms_%dm" % distance, "E[AoI] within %d m (ms)" % distance),
               ("pdrop", "Pdrop")]
    tones = [t for t in ["all", "range150", "range300", "range600", "power110", "power95"] if t in set(sub.tone)]
    for j in (1, 36):
        fig, axes = plt.subplots(1, 4, figsize=(17, 3.8))
        for ax, (metric, label) in zip(axes, metrics):
            for k, tone in enumerate(tones):
                c = curve(sub, scheme="rcs", groups=j, tone=tone)
                if len(c):
                    ax.plot(c.index, c[metric], marker=MARKERS[k], color=COLOURS[k], label="RCS tones: " + tone)
            for k, (scheme, _, name) in enumerate(SCHEMES[2:]):
                c = curve(sub, scheme=scheme)
                if len(c):
                    ax.plot(c.index, c[metric], linestyle="--", color="0.%d" % (2 + 3 * k), label=name)
            ax.set_xlabel("load a")
            ax.set_ylabel(label)
            ax.grid(alpha=0.3)
        axes[0].legend(fontsize=7)
        fig.suptitle("Highway (5 km, PHY), RCS J = %d with each tone setting, mu = 1" % j)
        fig.tight_layout()
        fig.savefig(os.path.join(out, "highway_tones_J%d.png" % j), dpi=150)
        plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("summary")
    parser.add_argument("--out", default="docs/figures/rcs")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    frame = pd.read_csv(args.summary)
    j_sweep(frame, args.out)
    comparison(frame, args.out)
    comparison(frame, args.out, "highway", "_300m")
    highway_tones(frame, args.out)
    print("figures in", args.out)


if __name__ == "__main__":
    main()
