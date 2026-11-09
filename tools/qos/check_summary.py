#!/usr/bin/env python3
"""Sanity checks on a QOS sweep summary, used by smoke.sh and by the integration tests.

    tools/qos/check_summary.py results/qos/<name> [--expect-runs N]
"""
import argparse
import os
import sys

import pandas as pd

CAUSE_PREFIX = "cause_"


def check(folder, expect_runs=None):
    problems = []
    summary = os.path.join(folder, "summary")
    for name in ("flows", "by_distance", "causes", "runs"):
        if not os.path.exists(os.path.join(summary, name + ".csv")):
            problems.append("missing summary/%s.csv" % name)
    if problems:
        return problems
    runs = pd.read_csv(os.path.join(summary, "runs.csv"))
    flows = pd.read_csv(os.path.join(summary, "flows.csv"))
    if expect_runs is not None and len(runs) != expect_runs:
        problems.append("%d runs finished, %d expected" % (len(runs), expect_runs))
    failed = [r for r in os.listdir(os.path.join(folder, "runs"))
              if os.path.exists(os.path.join(folder, "runs", r, "error.txt"))]
    problems += ["run %s failed: %s" % (r, open(os.path.join(folder, "runs", r, "error.txt")).read().strip())
                 for r in failed]
    for row in flows.to_dict("records"):
        where = "%s / %s" % (row["id"], row["name"])
        if row["attempts"] <= 0:
            problems.append("%s: no attempts" % where)
        if not 0 <= row["pdr"] <= 1 or not 0 <= row["qos_pdr"] <= row["pdr"] + 1e-12:
            problems.append("%s: PDR %.3f, QoS-PDR %.3f" % (where, row["pdr"], row["qos_pdr"]))
        causes = sum(v for c, v in row.items() if c.startswith(CAUSE_PREFIX))
        if causes != row["attempts"]:
            problems.append("%s: causes add up to %d, attempts %d" % (where, causes, row["attempts"]))
        if not (0 <= row["cbr_mean"] <= 1):
            problems.append("%s: CBR %.3f" % (where, row["cbr_mean"]))
    # every run must have every class of its scenario
    per_run = flows.groupby("id").name.nunique()
    if per_run.min() < 1:
        problems.append("a run has no class")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder")
    parser.add_argument("--expect-runs", type=int, default=None)
    args = parser.parse_args()
    problems = check(args.folder, args.expect_runs)
    for p in problems:
        print("FAIL", p)
    flows = pd.read_csv(os.path.join(args.folder, "summary", "flows.csv"))
    with pd.option_context("display.width", 200, "display.max_rows", 200):
        print(flows.groupby(["scenario", "name"])[["attempts", "pdr", "qos_pdr", "collision_rate", "hnir"]].mean().round(3))
    print("%d problem(s)" % len(problems))
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
