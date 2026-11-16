#!/usr/bin/env python3
"""Leaves superseded packets out of the metrics of runs summarised before metrics.py did so.

    tools/qos/exclude_superseded.py results/qos/<sweep>          then sweep.py --summarise

A packet replaced in the RLC queue by a newer packet of its class ("replaced by a newer packet")
used to count as a failed attempt. metrics.py now leaves it out of every metric and only counts
it in a "superseded" column. This script applies the same change to the per-run metric files of
an older sweep, from those files alone (the raw logs are usually deleted):

  flows.csv        attempts, PDR, QoS-PDR, PER, collision rate and HNIR without them, a
                   "superseded" column instead of the cause column. HNCR is unchanged.
  by_distance.csv  the same per distance bin, with a "superseded" column
  causes.csv       the "replaced by a newer packet" rows removed

Exact for everything above (checked against metrics.py on the same raw logs). PIR and AoI do
not change. DT (disconnection time) cannot be redone without the raw logs: a superseded packet
counted as a loss inside a DT interval, and that changes the DT of the flow a lot. So for every
flow that had superseded packets the DT columns of flows.csv are set to NaN, its DT histogram
rows are removed, and "dt_needs_rerun" is true. Rerunning the sweep with the current metrics.py
gives them back. A run already in the new format is left as it is.
"""
import argparse
import glob
import os
import sys

import pandas as pd

SUPERSEDED = "replaced by a newer packet"
CAUSE_COLUMN = "cause_" + SUPERSEDED


def rescale(frame, attempts, kept, columns):
    """Rates over all attempts -> rates over the kept ones (superseded packets are neither
    decoded, collided nor hidden, so the numerators do not change)."""
    factor = (attempts / kept).where(kept > 0)
    for column in columns:
        frame[column] = frame[column] * factor


def convert(folder):
    flows_path = os.path.join(folder, "flows.csv")
    flows = pd.read_csv(flows_path)
    if "superseded" in flows.columns:
        return False
    causes = pd.read_csv(os.path.join(folder, "causes.csv"))
    by_distance = pd.read_csv(os.path.join(folder, "by_distance.csv"))

    replaced = flows[CAUSE_COLUMN] if CAUSE_COLUMN in flows else pd.Series(0, index=flows.index)
    attempts = flows.attempts.astype(float)
    kept = attempts - replaced
    rescale(flows, attempts, kept, ["pdr", "qos_pdr", "collision_rate", "hnir"])
    flows["per_measured"] = 1 - flows.qos_pdr
    flows["per_met"] = flows.per_measured <= flows.per_target
    flows["attempts"] = kept.astype(int)
    flows.insert(flows.columns.get_loc("attempts") + 1, "superseded", replaced.astype(int))
    flows["dt_needs_rerun"] = replaced > 0
    dt_columns = [c for c in flows.columns if c.startswith("dt_") and c != "dt_needs_rerun"]
    flows.loc[flows.dt_needs_rerun, dt_columns] = float("nan")
    flows = flows.drop(columns=[CAUSE_COLUMN], errors="ignore").copy()
    histograms_path = os.path.join(folder, "histograms.csv")
    if os.path.exists(histograms_path):
        histograms = pd.read_csv(histograms_path)
        stale = histograms.metric.eq("dt") & histograms.flow.isin(flows.flow[flows.dt_needs_rerun])
        histograms[~stale].to_csv(histograms_path, index=False)

    per_bin = causes[causes.cause == SUPERSEDED].groupby(["flow", "bin"])["count"].sum().rename("superseded")
    by_distance = by_distance.join(per_bin, on=["flow", "bin"])
    by_distance["superseded"] = by_distance.superseded.fillna(0).astype(int)
    attempts = by_distance.attempts.astype(float)
    kept = attempts - by_distance.superseded
    rescale(by_distance, attempts, kept, ["pdr", "qos_pdr", "collision", "hidden"])
    by_distance["attempts"] = kept.astype(int)
    by_distance = by_distance[by_distance.attempts > 0]

    flows.to_csv(flows_path, index=False)
    by_distance.to_csv(os.path.join(folder, "by_distance.csv"), index=False)
    causes[causes.cause != SUPERSEDED].to_csv(os.path.join(folder, "causes.csv"), index=False)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sweep", help="results/qos/<sweep name>, or one run's metrics folder")
    args = parser.parse_args()
    folders = [args.sweep] if os.path.exists(os.path.join(args.sweep, "flows.csv")) else \
        sorted(glob.glob(os.path.join(args.sweep, "runs", "*", "metrics")))
    if not folders:
        sys.exit("no metrics folder under %s" % args.sweep)
    changed = sum(convert(f) for f in folders)
    print("%d of %d runs converted, %d already without superseded packets" % (changed, len(folders), len(folders) - changed))


if __name__ == "__main__":
    main()
