#!/usr/bin/env python3
"""Compare two sweeps of the same YAML statistically, for example before and after a port.

    tools/compare_sweeps.py results/baseline/sps results/validation/sps --csv out.csv

Hashes cannot cross ns-3 versions because the random draws change, so each KPI is compared
over the seeds. A difference counts when the 95 % confidence intervals of the two means do
not overlap. Seeds are not paired: a run number gives different draws in each version.
"""
import argparse
import glob
import os
import re
import sys

import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import morev2x_results as mr  # noqa: E402

T_95 = {2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 8: 2.36, 10: 2.26}


def kpis(folder):
    run = mr.Run(folder)
    rx = run.rx[run.rx.rxTime >= 1.0]  # applications start during the first second
    copies = mr.first_copies(rx)
    row = {"collisions": (rx.lossType == 3).mean(), "below sensitivity": (rx.lossType == 0).mean(),
           "half duplex": (rx.lossType == 1).mean(), "propagation": (rx.lossType == 2).mean(),
           "CBR": run.cbr[run.cbr.time >= 1.0].cbr.mean(),
           "latency mean ms": rx[rx.decoded == 1].latency.mean() * 1000,
           "selections": run.ssps.groupby(["node", "time"]).ngroups, "transmissions": len(run.tx)}
    edges = [0, 100, 300, 500, 800, 1200, 2000]
    for low, high in zip(edges, edges[1:]):
        band = copies[(copies.distance >= low) & (copies.distance < high)]
        row["PDR %d-%d m" % (low, high)] = band.decoded.mean()
    return row


def collect(sweep):
    if sweep.endswith(".csv"):
        return pd.read_csv(sweep)  # KPIs saved by an earlier --save
    rows = []
    for folder in sorted(glob.glob(os.path.join(sweep, "*", "results", "*"))):
        name = folder.split(os.sep)[-3]
        group = re.sub(r"_?runNo\d+", "", name) or "all"
        rows.append(dict(kpis(folder), group=group))
    return pd.DataFrame(rows)


def interval(values):
    n = len(values)
    half = T_95.get(n, 2.0) * values.std(ddof=1) / np.sqrt(n) if n > 1 else float("nan")
    return values.mean(), half, n


def compare(before, after):
    rows = []
    for group in sorted(set(before.group) & set(after.group)):
        a, b = before[before.group == group], after[after.group == group]
        for kpi in [c for c in before.columns if c != "group"]:
            ma, ha, na = interval(a[kpi].dropna())
            mb, hb, nb = interval(b[kpi].dropna())
            rows.append({"group": group, "kpi": kpi, "before": ma, "before_ci": ha, "after": mb, "after_ci": hb,
                         "runs": min(na, nb), "differs": bool(abs(ma - mb) > ha + hb)})
    return pd.DataFrame(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--csv", help="write the comparison here")
    parser.add_argument("--save", help="write the KPIs of the after sweep here, so its logs can go")
    args = parser.parse_args()
    after = collect(args.after)
    if args.save:
        after.to_csv(args.save, index=False)
    table = compare(collect(args.before), after)
    if args.csv:
        table.to_csv(args.csv, index=False)
    pd.set_option("display.width", 150, "display.max_rows", 500)
    print(table.round(4).to_string(index=False))
    print("%d of %d comparisons differ" % (table.differs.sum(), len(table)))


if __name__ == "__main__":
    main()
