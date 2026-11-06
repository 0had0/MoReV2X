#!/usr/bin/env python3
"""Run HIGHWAY over every combination of a YAML file and summarise the results.

    tools/run_sweep.py scenarios/sweeps/rsrp_density.yaml --jobs 5

Each key of the file is a HIGHWAY option, and a list is swept. Results go to
results/sweeps/<file name>/. A finished combination is not run again, so an
interrupted sweep resumes. The KPIs of every run are in summary.csv.

Based on run_simulator.py from kaefcatcher/MoReV2X_V2.
"""
import argparse
import itertools
import multiprocessing
import os
import sys

import pandas as pd
import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import morev2x_results as mr  # noqa: E402

PATH_OPTIONS = ("sumoCfg", "mobilityTrace")


def combinations(config):
    """Yields one {option: value} per point of the sweep, in a stable order."""
    keys = list(config)
    lists = [config[k] if isinstance(config[k], list) else [config[k]] for k in keys]
    for values in itertools.product(*lists):
        yield dict(zip(keys, values))


def to_arguments(point):
    arguments = []
    for key, value in point.items():
        if isinstance(value, bool):
            value = "true" if value else "false"
        elif key in PATH_OPTIONS:
            value = os.path.abspath(str(value))  # runs start in their own folder
        arguments.append("--%s=%s" % (key, value))
    return arguments


def swept_keys(config):
    return [k for k, v in config.items() if isinstance(v, list) and len(v) > 1]


def label(point, keys):
    return "_".join("%s%s" % (k, point[k]) for k in keys) or "single"


def run_point(job):
    point, workdir = job
    row = dict(point)
    try:
        row.update(mr.summary(mr.simulate(None, *to_arguments(point), workdir=workdir)).to_dict())
        row["failed"] = False
    except Exception as error:  # one bad point must not lose the rest of the sweep
        print("FAILED %s: %s" % (workdir, error), file=sys.stderr)
        row["failed"] = True
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("config")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--output", help="default: results/sweeps/<config name>")
    args = parser.parse_args()

    with open(args.config) as stream:
        config = yaml.safe_load(stream)
    name = os.path.splitext(os.path.basename(args.config))[0]
    output = os.path.abspath(args.output or os.path.join(mr.ROOT, "results", "sweeps", name))
    keys = swept_keys(config)
    jobs = [(point, os.path.join(output, label(point, keys))) for point in combinations(config)]
    print("%d runs, %d at a time, in %s" % (len(jobs), args.jobs, output))

    rows = []
    with multiprocessing.Pool(args.jobs) as pool:
        for count, row in enumerate(pool.imap_unordered(run_point, jobs), 1):
            rows.append(row)
            print("%d/%d %s" % (count, len(jobs), label(row, keys)), flush=True)

    os.makedirs(output, exist_ok=True)
    summary = pd.DataFrame(rows).sort_values(keys or list(rows[0])[:1])
    summary.to_csv(os.path.join(output, "summary.csv"), index=False)
    failed = int(summary.failed.sum())
    print("summary.csv written, %d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
