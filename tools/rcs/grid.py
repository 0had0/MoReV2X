#!/usr/bin/env python3
"""Run the RCS paper's grid (Rolich et al., MAIN 2026, arXiv:2607.06103) with MoReV2X.

    tools/rcs/grid.py --jobs 30                 # run everything, resumable
    tools/rcs/grid.py --list                    # print the runs in queue order
    tools/rcs/grid.py --summary                 # gather metrics.json into summary.csv

Every run gets results/rcs/<name>/: HIGHWAY runs there, tools/rcs/metrics.py writes
metrics.json, and the large logs are deleted. A run with metrics.json is not run again.

Paper setup (Table II): 20 MHz, sub-channels of 25 RBs, 200 B packets every 100 ms,
TDB = 60 ms, 50 s. Load a = n Tf / (K Tg) in 0.3 .. 1.1, so n = 360 a for RCS (K = 36)
and n = 400 a for SPS and DS (K = 40). RCS: r = 3, J in {1, 2, 3, 6, 9, 18, 36},
M = 400 (mu 0) or 200 (mu 1). SPS: RRI 100 ms, RC in [5, 15], Ppers 0 or 0.8. DS.

Two models:
  ideal    the paper's: static UEs in a 10 m square, no shadowing, metrics from the
           transmission logs (all-or-nothing, everybody hears everybody)
  highway  MoReV2X's 5 km highway with the PHY; the RCS tones are heard by all, within
           a range, or above a received power, to see how RCS fares when they are not
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import time
from multiprocessing import Pool

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOADS = [round(0.3 + 0.1 * i, 1) for i in range(9)]
GROUPS = [1, 2, 3, 6, 9, 18, 36]
SLOT_MS = {0: 1.0, 1: 0.5}
BASELINES = {"sps0": ["--pkeep=0"], "sps08": ["--pkeep=0.8"], "ds": ["--Dynamic=1"]}
TONES = {"all": ["--RcsTone=all"],
         "range150": ["--RcsTone=range", "--RcsToneRange=150"],
         "range300": ["--RcsTone=range", "--RcsToneRange=300"],
         "range600": ["--RcsTone=range", "--RcsToneRange=600"],
         "power110": ["--RcsTone=power", "--RcsToneThreshold=-110"],
         "power95": ["--RcsTone=power", "--RcsToneThreshold=-95"]}
LARGE_LOGS = ["ReceivedLog.txt", "TxFile.txt", "RxFile.txt", "phyDebugShort.txt", "RcsLog.txt",
              "SSPSlog.txt", "ReEvaluationsLog.txt", "posFile.txt", "tbRxFile.csv"]


def run(name, model, scheme, mu, load, seed, groups=None, tone=None):
    k = 36 if scheme == "rcs" else 40
    n = int(round(load * k * 100 / 10))  # a = n Tf / (K Tg), Tf = 10 ms, Tg = 100 ms
    arguments = ["--ChannelBW=20", "--SubChannel=25", "--PDB=60", "--Periodic=1", "--UniformPeriod=100",
                 "--mcs=13", "--UdpFiles=1", "--RxFile=0", "--Numerology=%d" % mu, "--Vehicles=%d" % n,
                 "--seed=%d" % seed, "--runNo=1"]
    arguments += ["--Ideal=1", "--RxLog=0"] if model == "ideal" else ["--RxLog=1"]
    if scheme == "rcs":
        arguments += ["--Rcs=1", "--RcsGroups=%d" % groups] + TONES[tone or "all"]
    else:
        arguments += BASELINES[scheme]
    # The paper's Figs 7 and 8 (ideal, mu = 1, RCS J = 1 and 36, SPS, DS) and the highway get the
    # full simulation time; the rest of the J sweep the short one (the ideal channel makes every
    # UE decode every transmission, so a dense 50 s run takes hours)
    headline = model == "highway" or (mu == 1 and (scheme != "rcs" or groups in (1, 36)))
    return {"name": name, "model": model, "scheme": scheme, "mu": mu, "load": load, "n": n, "seed": seed,
            "groups": groups, "tone": tone, "headline": headline, "arguments": arguments}


def grid(seeds):
    """The runs in queue order: what the paper's figures need first."""
    queue = []
    for seed in seeds:
        tag = "s%d" % seed
        ideal = []
        # Figs 7, 8: RCS J = 1 and 36 against SPS and DS at mu = 1
        for load in LOADS:
            for j in (1, 36):
                ideal.append(run("ideal_rcs_J%d_mu1_a%.1f_%s" % (j, load, tag), "ideal", "rcs", 1, load, seed, j))
            for scheme in BASELINES:
                ideal.append(run("ideal_%s_mu1_a%.1f_%s" % (scheme, load, tag), "ideal", scheme, 1, load, seed))
        # Figs 2-6: the J sweep at mu = 0 and 1
        for mu in (1, 0):
            for j in GROUPS:
                for load in LOADS:
                    name = "ideal_rcs_J%d_mu%d_a%.1f_%s" % (j, mu, load, tag)
                    if not any(r["name"] == name for r in ideal):
                        ideal.append(run(name, "ideal", "rcs", mu, load, seed, j))
        highway = []
        for load in LOADS:
            for j in (1, 36):
                for tone in TONES:
                    highway.append(run("highway_rcs_J%d_%s_mu1_a%.1f_%s" % (j, tone, load, tag),
                                       "highway", "rcs", 1, load, seed, j, tone))
            for scheme in BASELINES:
                highway.append(run("highway_%s_mu1_a%.1f_%s" % (scheme, load, tag), "highway", scheme, 1, load, seed))
        # DS and SPS do not depend on mu in the paper; checked at the end
        late = [run("ideal_%s_mu0_a%.1f_%s" % (scheme, load, tag), "ideal", scheme, 0, load, seed)
                for load in LOADS for scheme in BASELINES]
        queue += ideal + highway + late
    return queue


def longest_first(queue, sim_time, short_sim_time):
    """Longest runs first, so the grid ends soonest. Cost ~ time x n^2; the highway's PHY
    skips the far receivers and runs about 8 times faster than the ideal square."""
    def cost(r):
        t = sim_time if r["headline"] or not short_sim_time else short_sim_time
        return t * r["n"] ** 2 / (8.0 if r["model"] == "highway" else 1.0)
    return sorted(queue, key=lambda r: (r["seed"], -cost(r)))


def execute(spec, root, sim_time, binary, short_sim_time=None):
    folder = os.path.join(root, spec["name"])
    metrics = os.path.join(folder, "metrics.json")
    if os.path.exists(metrics):
        return spec["name"], "done before", 0.0
    shutil.rmtree(os.path.join(folder, "results"), ignore_errors=True)
    os.makedirs(folder, exist_ok=True)
    if short_sim_time and not spec["headline"]:
        sim_time = short_sim_time
    arguments = spec["arguments"] + ["--simTime=%g" % sim_time]
    with open(os.path.join(folder, "spec.json"), "w") as f:
        json.dump(dict(spec, arguments=arguments), f, indent=1)
    environment = dict(os.environ, LD_LIBRARY_PATH=os.path.dirname(os.path.dirname(binary)),
                       DYLD_LIBRARY_PATH=os.path.dirname(os.path.dirname(binary)))
    start = time.time()
    with open(os.path.join(folder, "stderr.log"), "w") as err:
        status = subprocess.run([binary] + arguments, cwd=folder, env=environment,
                                stdout=subprocess.DEVNULL, stderr=err).returncode
    wall = time.time() - start
    if status != 0:
        return spec["name"], "failed (%d)" % status, wall
    out = glob.glob(os.path.join(folder, "results", "*"))[0]
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "rcs", "metrics.py"), out,
                    "--model", spec["model"], "--slot-ms", str(SLOT_MS[spec["mu"]]), "--pdb", "60",
                    "--out", metrics + ".tmp"], check=True)
    with open(metrics + ".tmp") as f:
        values = json.load(f)
    values.update({k: spec[k] for k in ("name", "model", "scheme", "mu", "load", "n", "seed", "groups", "tone")})
    values["wall_s"] = wall
    values["sim_time"] = sim_time
    with open(metrics + ".tmp", "w") as f:
        json.dump(values, f, indent=1)
    os.replace(metrics + ".tmp", metrics)
    for name in LARGE_LOGS:
        path = os.path.join(out, name)
        if os.path.exists(path):
            os.remove(path)
    return spec["name"], "ok", wall


def safe_execute(task):
    try:
        return execute(*task)
    except Exception as error:  # one broken run should not stop the grid
        return task[0]["name"], "error: %s" % error, 0.0


def summary(root):
    import pandas as pd
    rows = []
    for path in sorted(glob.glob(os.path.join(root, "*", "metrics.json"))):
        with open(path) as f:
            values = json.load(f)
        values.pop("prr_by_distance", None)
        values.pop("window_s", None)
        rows.append(values)
    frame = pd.DataFrame(rows)
    frame.to_csv(os.path.join(root, "summary.csv"), index=False)
    return frame


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() - 2))
    parser.add_argument("--root", default=os.path.join(ROOT, "results", "rcs"))
    parser.add_argument("--sim-time", type=float, default=50.0)
    parser.add_argument("--short-sim-time", type=float, help="simulation time of the runs outside Figs 7, 8 and the highway")
    parser.add_argument("--headline", action="store_true", help="only the runs of Figs 7, 8 and the highway")
    parser.add_argument("--seeds", type=int, nargs="+", default=[1])
    parser.add_argument("--only", help="run the names that contain this text")
    parser.add_argument("--binary", default=os.path.join(ROOT, "build", "optimized", "scratch", "HIGHWAY"))
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()
    queue = grid(args.seeds)
    if args.only:
        queue = [r for r in queue if args.only in r["name"]]
    if args.headline:
        queue = [r for r in queue if r["headline"]]
    if args.list:
        for r in queue:
            print(r["name"], " ".join(r["arguments"]))
        print(len(queue), "runs")
        return
    if args.summary:
        print(summary(args.root).groupby(["model", "scheme"]).size())
        return
    os.makedirs(args.root, exist_ok=True)
    log = open(os.path.join(args.root, "progress.log"), "a")
    log.write("%s start %d runs, %d jobs\n" % (time.strftime("%F %T"), len(queue), args.jobs))
    log.flush()
    queue = longest_first(queue, args.sim_time, args.short_sim_time)
    tasks = [(r, args.root, args.sim_time, args.binary, args.short_sim_time) for r in queue]
    with Pool(args.jobs) as pool:
        for i, (name, status, wall) in enumerate(pool.imap_unordered(safe_execute, tasks)):
            log.write("%s %d/%d %s %s %.0f s\n" % (time.strftime("%F %T"), i + 1, len(queue), name, status, wall))
            log.flush()
    summary(args.root)
    log.write("%s finished\n" % time.strftime("%F %T"))


if __name__ == "__main__":
    main()
