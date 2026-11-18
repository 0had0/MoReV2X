#!/usr/bin/env python3
"""The RCS priority experiment: two flows of different priority, the 3GPP sensing-based selection
against Repeated Contention Scheduling (Rolich et al., MAIN 2026) with uniform or priority-skewed
tones. Runs scratch/QOS.

    tools/rcs/priority_grid.py --jobs 30 --seeds 1 2     # run everything (192 runs), resumable
    tools/rcs/priority_grid.py --list                    # print the runs in queue order
    tools/rcs/priority_grid.py --summary                 # gather metrics.json into summary.csv

Flows (scenarios/rcs/priority_classes.yaml)
  periodic1   PQI 90, SL priority 5, PDB 100 ms: every vehicle, the whole run, SB-SPS (RRI 100 ms)
  aperiodic1  PQI 89, SL priority 2, PDB 50 ms: 70 % of the vehicles from BURST[0] to BURST[1] s,
              dynamic selection; its sizes are capped at APERIODIC1_MAX B so a packet fits one
              RCS sub-channel (25 RBs at MCS 13 carry 509 B of payload)

Radio, as the paper's Figs 7 and 8: mu = 1, 20 MHz, sub-channels of 25 RBs, MCS 13. Vehicles
n = 108, 180, 252: periodic1 alone is then a = 0.3, 0.5, 0.7 of RCS's K = 36 SCs per 10 ms.

Schemes
  3gpp_off             sensing-based selection, no re-evaluation, no pre-emption
  3gpp_reeval          with re-evaluation
  3gpp_reeval_preempt  with re-evaluation and pre-emption: the baseline
  3gpp_*_retx1         the same, aperiodic1 with one blind retransmission. Pre-emption reacts to
                       resources announced in a higher-priority SCI only; a dynamic one-shot
                       packet announces none, its retransmission does
  rcs_J<j>_<choice>_<tones>   RCS with J groups, uniform or skewed tones (NrV2XRcsSkewedChoice),
                       tones heard by all or within a range

Models
  square   the paper's: static vehicles in a 10 m square, no shadowing, all-or-nothing reception;
           RCS tones heard by all
  highway  SUMO highway of 2 km, 3 lanes per direction, the PHY decides, pairs within 300 m;
           RCS tones heard by all, within 600, 300 or 150 m

Every run gets <root>/<name>/: the flows file, QOS's logs in logs/, tools/rcs/priority_metrics.py's
tables in metrics/ and metrics.json. The large logs are deleted. A run with metrics.json is skipped.
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

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "qos"))
import sweep  # noqa: E402

VEHICLES = {0.3: 108, 0.5: 180, 0.7: 252}
SIM_TIME = 30.0
BURST = (10.0, 20.0)
WARMUP = 2.0
APERIODIC1_MAX = 400
HIGHWAY_LENGTH = 2000.0
HIGHWAY_LANES = 6
RADIO = ["--Numerology=1", "--ChannelBW=20", "--SubChannel=25", "--mcs=13", "--SavingPeriod=0.5"]
POLICIES = {"3gpp_off": ["--ReEvaluation=0", "--Preemption=0"],
            "3gpp_reeval": ["--ReEvaluation=1", "--Preemption=0"],
            "3gpp_reeval_preempt": ["--ReEvaluation=1", "--Preemption=1"]}
TONES = {"all": ["--RcsTone=all"],
         "range600": ["--RcsTone=range", "--RcsToneRange=600"],
         "range300": ["--RcsTone=range", "--RcsToneRange=300"],
         "range150": ["--RcsTone=range", "--RcsToneRange=150"]}
LARGE_LOGS = ["ReceivedLog.txt", "phyDebugShort.txt", "RcsLog.txt", "RcsContentionLog.txt", "SSPSlog.txt",
              "ReEvaluationsLog.txt", "ReservationsLog.txt", "AppTxLog.txt", "MacDropLog.txt", "CBR_OutputFile.txt"]


def grid(seeds):
    runs = []
    for seed in seeds:
        for load, n in VEHICLES.items():
            for model in ("square", "highway"):
                common = dict(model=model, load=load, n=n, seed=seed)
                for policy, arguments in POLICIES.items():
                    for retx in (0, 1):
                        scheme = policy + ("_retx1" if retx else "")
                        runs.append(dict(common, name="%s_%s_a%.1f_s%d" % (model, scheme, load, seed), scheme=scheme,
                                         groups=None, choice=None, tone=None, retx=retx, arguments=list(arguments)))
                tones = ["all"] if model == "square" else list(TONES)
                for j in (1, 36):
                    for choice in ("uniform", "skewed"):
                        for tone in tones:
                            runs.append(dict(common, name="%s_rcs_J%d_%s_%s_a%.1f_s%d" % (model, j, choice, tone, load, seed),
                                             scheme="rcs", groups=j, choice=choice, tone=tone, retx=0,
                                             arguments=["--Rcs=1", "--RcsGroups=%d" % j, "--RcsChoice=" + choice] + TONES[tone]))
    return runs


def cost(run):
    """Wall time ~ n^2 x simulated time; the highway's PHY skips the far receivers."""
    return run["n"] ** 2 / (6.0 if run["model"] == "highway" else 1.0)


def trace_folder(run, cache, sim_time):
    return os.path.join(cache, "%s_n%d_s%d_t%g" % (run["model"], run["n"], run["seed"], sim_time))


def trace(run, cache, sim_time):
    """The mobility trace of the run, made once per (model, n, seed, duration)."""
    folder = trace_folder(run, cache, sim_time)
    if os.path.exists(os.path.join(folder, "roles.csv")):
        return folder
    temporary = folder + ".tmp"
    shutil.rmtree(temporary, ignore_errors=True)
    if run["model"] == "square":
        command = [sys.executable, os.path.join(ROOT, "tools", "rcs", "static_square.py"), "--vehicles", str(run["n"]),
                   "--duration", str(sim_time + 1), "--seed", str(run["seed"]), "--out", temporary]
    else:
        density = run["n"] / (HIGHWAY_LENGTH / 1000.0 * HIGHWAY_LANES)
        command = [sys.executable, os.path.join(ROOT, "tools", "qos", "make_traces.py"), "highway_cpm",
                   "--density", "%g" % density, "--length", "%g" % HIGHWAY_LENGTH, "--duration", str(sim_time + 1),
                   "--seed", str(run["seed"]), "--out", temporary]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    shutil.rmtree(folder, ignore_errors=True)
    os.replace(temporary, folder)
    return folder


def flows_file(path, burst, retx=0):
    classes = yaml.safe_load(open(os.path.join(ROOT, "scenarios", "rcs", "priority_classes.yaml")))["classes"]
    overrides = {"aperiodic1": {"traffic": "aperiodic1:%d" % APERIODIC1_MAX, "start_s": burst[0], "stop_s": burst[1], "retx": retx}}
    rows = [sweep.flow_row(name, classes[name], overrides) for name in classes]
    with open(path, "w") as f:
        f.write(",".join(sweep.FLOW_HEADER) + "\n")
        for row in rows:
            f.write(",".join(str(v) for v in row) + "\n")


def execute(run, root, binary, traces, sim_time, burst):
    folder = os.path.join(root, run["name"])
    metrics = os.path.join(folder, "metrics.json")
    if os.path.exists(metrics):
        return run["name"], "done before", 0.0
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    trace_path = trace_folder(run, traces, sim_time)
    flows = os.path.join(folder, "flows.csv")
    flows_file(flows, burst, run["retx"])
    logs = os.path.join(folder, "logs") + "/"
    arguments = ["--trace=" + trace_path, "--flows=" + flows, "--out=" + logs, "--simTime=%g" % sim_time,
                 "--seed=%d" % run["seed"], "--runNo=1"] + RADIO + run["arguments"]
    if run["model"] == "square":
        arguments += ["--Ideal=1", "--RxLog=0"]
    else:
        arguments += ["--RxLog=1", "--ns3::NrV2XSpectrumPhy::MetricsMaxDistance=400"]
    with open(os.path.join(folder, "spec.json"), "w") as f:
        json.dump(dict(run, arguments=arguments, trace=trace_path, sim_time=sim_time, burst=burst), f, indent=1)
    environment = dict(os.environ, LD_LIBRARY_PATH=os.path.dirname(os.path.dirname(binary)),
                       DYLD_LIBRARY_PATH=os.path.dirname(os.path.dirname(binary)))
    start = time.time()
    with open(os.path.join(folder, "qos.log"), "w") as out:
        status = subprocess.run([binary] + arguments, cwd=folder, env=environment, stdout=out,
                                stderr=subprocess.STDOUT).returncode
    wall = time.time() - start
    if status != 0:
        return run["name"], "failed (%d)" % status, wall
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "rcs", "priority_metrics.py"), logs, trace_path,
                    "--model", run["model"], "--burst", str(burst[0]), str(burst[1]), "--warmup", str(WARMUP),
                    "--out", os.path.join(folder, "metrics")], check=True)
    with open(os.path.join(folder, "metrics", "metrics.json")) as f:
        values = json.load(f)
    values.update({k: run[k] for k in ("name", "model", "scheme", "load", "n", "seed", "groups", "choice", "tone", "retx")})
    values["wall_s"] = wall
    values["sim_time"] = sim_time
    with open(metrics + ".tmp", "w") as f:
        json.dump(values, f, indent=1)
    os.replace(metrics + ".tmp", metrics)
    for name in LARGE_LOGS:
        path = os.path.join(logs, name)
        if os.path.exists(path):
            os.remove(path)
    return run["name"], "ok", wall


def safe_execute(task):
    try:
        return execute(*task)
    except Exception as error:  # one broken run should not stop the grid
        return task[0]["name"], "error: %s" % error, 0.0


def summary(root):
    import pandas as pd
    rows = [json.load(open(path)) for path in sorted(glob.glob(os.path.join(root, "*", "metrics.json")))]
    frame = pd.DataFrame(rows)
    frame.to_csv(os.path.join(root, "summary.csv"), index=False)
    # the per-phase tables of every run, stacked, for the notebook
    for table in ("paper", "reevaluations", "rcs", "cbr_time", "resources", "resources_time"):
        parts = []
        for path in sorted(glob.glob(os.path.join(root, "*", "metrics", table + ".csv"))):
            if os.path.getsize(path) <= 1:
                continue
            part = pd.read_csv(path)
            part.insert(0, "run", os.path.basename(os.path.dirname(os.path.dirname(path))))
            parts.append(part)
        if parts:
            pd.concat(parts, ignore_index=True).to_csv(os.path.join(root, table + ".csv"), index=False)
    return frame


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() - 2))
    parser.add_argument("--root", default=os.path.join(ROOT, "results", "rcs_priority"))
    parser.add_argument("--traces", help="trace cache, default <root>/traces")
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 2])
    parser.add_argument("--only", help="run the names that contain this text")
    parser.add_argument("--binary", default=os.path.join(ROOT, "build", "optimized", "scratch", "QOS"))
    parser.add_argument("--sim-time", type=float, default=SIM_TIME)
    parser.add_argument("--burst", type=float, nargs=2, default=list(BURST), metavar=("START", "STOP"))
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()
    args.root = os.path.abspath(args.root)  # QOS runs in each run's folder
    args.binary = os.path.abspath(args.binary)
    queue = grid(args.seeds)
    if args.only:
        queue = [r for r in queue if args.only in r["name"]]
    queue = sorted(queue, key=lambda r: (r["seed"], -cost(r)))
    if args.list:
        for r in queue:
            print(r["name"], " ".join(r["arguments"]))
        print(len(queue), "runs")
        return
    if args.summary:
        print(summary(args.root).groupby(["model", "scheme"]).size())
        return
    traces = args.traces or os.path.join(args.root, "traces")
    os.makedirs(traces, exist_ok=True)
    log = open(os.path.join(args.root, "progress.log"), "a")
    log.write("%s start %d runs, %d jobs\n" % (time.strftime("%F %T"), len(queue), args.jobs))
    log.flush()
    made = set()
    for r in queue:  # traces first, one at a time: the runs share them
        key = trace_folder(r, traces, args.sim_time)
        if key not in made:
            trace(r, traces, args.sim_time)
            made.add(key)
    log.write("%s %d traces ready\n" % (time.strftime("%F %T"), len(made)))
    log.flush()
    tasks = [(r, args.root, args.binary, traces, args.sim_time, tuple(args.burst)) for r in queue]
    with Pool(args.jobs) as pool:
        for i, (name, status, wall) in enumerate(pool.imap_unordered(safe_execute, tasks)):
            log.write("%s %d/%d %s %s %.0f s\n" % (time.strftime("%F %T"), i + 1, len(queue), name, status, wall))
            log.flush()
    summary(args.root)
    log.write("%s finished\n" % time.strftime("%F %T"))


if __name__ == "__main__":
    main()
