#!/usr/bin/env python3
"""Run a QOS sweep described by a YAML file: traces, simulations, metrics, one summary.

    tools/qos/sweep.py scenarios/qos/sweep_full.yaml --jobs 30
    tools/qos/sweep.py scenarios/qos/sweep_full.yaml --dry-run          list the runs, run nothing
    tools/qos/sweep.py scenarios/qos/sweep_smoke.yaml --jobs 4          what tools/qos/smoke.sh runs
    tools/qos/sweep.py <yaml> --only scenario=highway_platoon,policy=reeval
    tools/qos/sweep.py <yaml> --summarise                               rebuild the summary only

Everything goes to results/qos/<name>/:
  traces/<scenario>_<density>_<seed>/   make_traces.py output, made once and reused
  flows/<scenario>_<variant>.csv        the classes given to QOS
  runs/<run id>/                        QOS logs (deleted after the metrics unless keep_raw),
                                        metrics/ (kept), stdout.log, done
  progress.json                         done, failed, running, total, ETA. Read by monitor.sh
  summary/                              every run's metrics/*.csv concatenated, with the run
                                        parameters as columns: flows.csv, by_distance.csv,
                                        causes.csv, histograms.csv, cbr_time.csv, runs.csv

A run is done when runs/<id>/done exists, so an interrupted sweep resumes where it stopped.
The run id is a readable list of its parameters: <scenario>_<density>_<condition>_<variant>_<policy>_s<seed>.
"""
import argparse
import concurrent.futures as futures
import itertools
import json
import os
import shutil
import subprocess
import sys
import time

import pandas as pd
import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "qos"))
import metrics  # noqa: E402

FLOW_HEADER = ["name", "pqi", "priority", "pdb_ms", "per", "gbr", "roles", "share", "model", "period_ms",
               "exp_mean_ms", "event_rate", "burst", "burst_interval_ms", "sizes", "size_probs",
               "reservation_size", "rri_ms", "scheduling", "stack_delay_ms", "stack_jitter_ms", "retx"]
RAW_LOGS = ["ReceivedLog.txt", "phyDebugShort.txt", "SSPSlog.txt", "ReEvaluationsLog.txt", "CBR_OutputFile.txt",
            "ReservationsLog.txt", "AppTxLog.txt", "MacDropLog.txt"]


def load_config(path):
    """The sweep YAML, with the files listed under include: merged in (keys of the sweep win)."""
    config = yaml.safe_load(open(path))
    for name in config.pop("include", []):
        extra = yaml.safe_load(open(os.path.join(os.path.dirname(os.path.abspath(path)), name)))
        for key, value in extra.items():
            if isinstance(value, dict):
                merged = dict(value)
                merged.update(config.get(key, {}))
                config[key] = merged
            else:
                config.setdefault(key, value)
    return config


def flow_row(name, spec, overrides):
    spec = dict(spec, **overrides.get(name, {}))
    sizes = spec["sizes"]
    probs = spec.get("probs", [1.0 / len(sizes)] * len(sizes))
    return [name, spec["pqi"], spec["priority"], spec["pdb_ms"], spec["per"], int(spec.get("gbr", 0)),
            "|".join(spec["roles"]), spec.get("share", 1.0), spec["model"], spec.get("period_ms", 0),
            spec.get("exp_mean_ms", 0), spec.get("event_rate", 0), spec.get("burst", 1),
            spec.get("burst_interval_ms", 0), "|".join(str(s) for s in sizes), "|".join(str(p) for p in probs),
            spec.get("reservation_size", max(sizes)), spec["rri_ms"], spec.get("scheduling", "sps"),
            spec.get("stack_delay_ms", 0), spec.get("stack_jitter_ms", 0), spec.get("retx", 0)]


def expand(config):
    """Every run of the sweep, as a dict of its parameters."""
    runs = []
    seeds = config["seeds"]
    for scenario_name, scenario in config["scenarios"].items():
        conditions = scenario.get("conditions", {"baseline": {}})
        variants = scenario.get("variants", {"default": {}})
        policies = scenario.get("policies", list(config["policies"]))
        for density, (condition, cond), (variant, var), policy, seed in itertools.product(
                scenario["densities"], conditions.items(), variants.items(), policies, seeds):
            runs.append({"scenario": scenario_name, "density": density, "condition": condition, "variant": variant,
                         "policy": policy, "seed": seed, "condition_spec": cond or {}, "variant_spec": var or {}})
    for run in runs:
        run["id"] = "%s_%s_%s_%s_%s_s%d" % (run["scenario"], run["density"], run["condition"], run["variant"],
                                           run["policy"], run["seed"])
    return runs


def matches(run, only):
    for clause in only:
        key, value = clause.split("=", 1)
        if str(run.get(key)) not in value.split("|"):
            return False
    return True


def ensure_trace(config, out, run):
    scenario = config["scenarios"][run["scenario"]]
    trace = scenario["trace"]
    folder = os.path.join(out, "traces", "%s_%s_%d" % (run["scenario"], run["density"], run["seed"]))
    if os.path.exists(os.path.join(folder, "meta.json")):
        return folder
    command = [sys.executable, os.path.join(ROOT, "tools", "qos", "make_traces.py"), trace["scenario"],
               "--density", str(run["density"]), "--seed", str(run["seed"]),
               "--duration", str(config["sim_time"] + 2), "--out", folder]
    for key, value in trace.items():
        if key != "scenario":
            command += ["--" + key.replace("_", "-"), str(value)]
    subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return folder


def ensure_flows(config, out, run):
    scenario = config["scenarios"][run["scenario"]]
    overrides = {}
    for source in (run["variant_spec"].get("classes", {}), run["condition_spec"].get("classes", {})):
        for name, values in source.items():
            overrides.setdefault(name, {}).update(values)
    tag = "%s_%s_%s" % (run["scenario"], run["condition"], run["variant"])
    path = os.path.join(out, "flows", tag + ".csv")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    rows = [flow_row(name, config["classes"][name], overrides) for name in scenario["classes"]]
    temporary = "%s.%d.tmp" % (path, os.getpid())  # runs of one scenario write it in parallel
    pd.DataFrame(rows, columns=FLOW_HEADER).to_csv(temporary, index=False)
    os.replace(temporary, path)
    return path


def command_line(config, run, trace, flows, folder):
    scenario = config["scenarios"][run["scenario"]]
    binary = os.path.join(ROOT, config.get("binary", "build/optimized/scratch/QOS"))
    arguments = {"trace": trace, "flows": flows, "out": os.path.join(folder, "logs") + "/",
                 "simTime": config["sim_time"], "seed": run["seed"], "runNo": 1}
    arguments.update(config.get("phy", {}))
    arguments.update(scenario.get("arguments", {}))
    arguments.update(config["policies"][run["policy"]])
    for spec in (run["condition_spec"], run["variant_spec"]):
        arguments.update(spec.get("arguments", {}))
    line = [binary] + ["--%s=%s" % (k, v) for k, v in arguments.items()]
    line.append("--ns3::NrV2XSpectrumPhy::MetricsMaxDistance=%g" % config["max_distance"])
    return line


METRICS_GATE = None  # semaphore shared by the workers, set by init_worker


def init_worker(gate):
    global METRICS_GATE
    METRICS_GATE = gate


def execute(config, out, run):
    folder = os.path.join(out, "runs", run["id"])
    if os.path.exists(os.path.join(folder, "done")):
        return run["id"], "skipped", 0.0
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    os.makedirs(folder)
    start = time.time()
    try:
        trace = ensure_trace(config, out, run)
        flows = ensure_flows(config, out, run)
        line = command_line(config, run, trace, flows, folder)
        with open(os.path.join(folder, "command.txt"), "w") as handle:
            handle.write(" ".join(line) + "\n")
        environment = dict(os.environ)
        build = os.path.dirname(os.path.dirname(line[0]))
        environment["LD_LIBRARY_PATH"] = build + os.pathsep + environment.get("LD_LIBRARY_PATH", "")
        environment["DYLD_LIBRARY_PATH"] = build + os.pathsep + environment.get("DYLD_LIBRARY_PATH", "")
        with open(os.path.join(folder, "stdout.log"), "w") as log:
            subprocess.run(line, cwd=folder, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True,
                           timeout=config.get("timeout", 6 * 3600))
        # At most metrics_jobs extractions at once: each holds a few GB while simulations hold less
        if METRICS_GATE is not None:
            METRICS_GATE.acquire()
        try:
            metrics.summarise(os.path.join(folder, "logs"), trace, config["warmup"], config["max_distance"],
                              os.path.join(folder, "metrics"), keep_attempts=config.get("keep_attempts", False))
        finally:
            if METRICS_GATE is not None:
                METRICS_GATE.release()
        params = {k: v for k, v in run.items() if not k.endswith("_spec")}
        params["seconds"] = round(time.time() - start, 1)
        json.dump(params, open(os.path.join(folder, "metrics", "params.json"), "w"))
        if not config.get("keep_raw", False):
            for name in RAW_LOGS:
                path = os.path.join(folder, "logs", name)
                if os.path.exists(path):
                    os.remove(path)
        open(os.path.join(folder, "done"), "w").write("%.1f\n" % (time.time() - start))
        return run["id"], "ok", time.time() - start
    except Exception as error:  # the run stays without done, the next pass retries it
        with open(os.path.join(folder, "error.txt"), "w") as handle:
            handle.write("%s: %s\n" % (type(error).__name__, error))
        return run["id"], "failed", time.time() - start


def write_progress(out, total, done, failed, running, durations, started):
    remaining = total - done - failed
    mean = sum(durations) / len(durations) if durations else None
    state = {"total": total, "done": done, "failed": failed, "running": running, "remaining": remaining,
             "mean_run_s": round(mean, 1) if mean else None, "elapsed_s": round(time.time() - started),
             "updated": time.strftime("%Y-%m-%d %H:%M:%S")}
    json.dump(state, open(os.path.join(out, "progress.json.tmp"), "w"), indent=1)
    os.replace(os.path.join(out, "progress.json.tmp"), os.path.join(out, "progress.json"))


def summarise(out):
    """Concatenate the metrics of every finished run, with its parameters as columns."""
    target = os.path.join(out, "summary")
    os.makedirs(target, exist_ok=True)
    tables = {}
    runs = []
    for run_id in sorted(os.listdir(os.path.join(out, "runs"))):
        folder = os.path.join(out, "runs", run_id, "metrics")
        if not os.path.exists(os.path.join(out, "runs", run_id, "done")):
            continue
        params = json.load(open(os.path.join(folder, "params.json")))
        runs.append(params)
        for name in ("flows", "by_distance", "causes", "histograms", "cbr_time"):
            path = os.path.join(folder, name + ".csv")
            if os.path.exists(path) and os.path.getsize(path) > 0:
                frame = pd.read_csv(path)
                for key in ("id", "scenario", "density", "condition", "variant", "policy", "seed"):
                    frame[key] = params[key]
                tables.setdefault(name, []).append(frame)
    for name, frames in tables.items():
        pd.concat(frames, ignore_index=True).to_csv(os.path.join(target, name + ".csv"), index=False)
    pd.DataFrame(runs).to_csv(os.path.join(target, "runs.csv"), index=False)
    return len(runs)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("config")
    parser.add_argument("--jobs", type=int, default=None)
    parser.add_argument("--only", action="append", default=[], help="key=value[|value], e.g. policy=off|reeval")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--summarise", action="store_true")
    parser.add_argument("--out", default=None, help="default results/qos/<name>")
    args = parser.parse_args()

    config = load_config(args.config)
    out = args.out or os.path.join(ROOT, "results", "qos", config["name"])
    os.makedirs(out, exist_ok=True)
    # The merged configuration, includes resolved: what this sweep really ran
    yaml.safe_dump(config, open(os.path.join(out, "sweep.yaml"), "w"), sort_keys=False)
    if args.summarise:
        print("%d runs summarised in %s" % (summarise(out), os.path.join(out, "summary")))
        return

    runs = [r for r in expand(config) if matches(r, args.only)]
    if args.dry_run:
        for run in runs:
            print(run["id"])
        print("%d runs" % len(runs))
        return

    # Traces first, one process each: SUMO runs are short and the simulations need them
    for key in sorted({(r["scenario"], r["density"], r["seed"]) for r in runs}):
        run = next(r for r in runs if (r["scenario"], r["density"], r["seed"]) == key)
        print("trace", ensure_trace(config, out, run), flush=True)

    for run in runs:
        ensure_flows(config, out, run)

    jobs = args.jobs or config.get("jobs", os.cpu_count())
    started = time.time()
    done = failed = 0
    durations = []
    total = len(runs)
    write_progress(out, total, done, failed, 0, durations, started)
    import multiprocessing
    gate = multiprocessing.Manager().Semaphore(config.get("metrics_jobs", max(1, jobs // 4)))
    with futures.ProcessPoolExecutor(max_workers=jobs, initializer=init_worker, initargs=(gate,)) as pool:
        pending = {pool.submit(execute, config, out, run): run for run in runs}
        for future in futures.as_completed(pending):
            run_id, status, seconds = future.result()
            if status == "failed":
                failed += 1
            else:
                done += 1
                if status == "ok":
                    durations.append(seconds)
            running = min(jobs, total - done - failed)
            write_progress(out, total, done, failed, running, durations, started)
            print("%s %-8s %6.0f s  (%d/%d, %d failed)" % (time.strftime("%H:%M:%S"), status, seconds, done + failed,
                                                          total, failed), run_id, flush=True)
    print("%d runs summarised in %s" % (summarise(out), os.path.join(out, "summary")))
    if failed:
        print("%d runs failed, see runs/*/error.txt. Run the same command again to retry them." % failed)
        sys.exit(1)


if __name__ == "__main__":
    main()
