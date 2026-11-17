#!/usr/bin/env python3
"""Metrics of one run of the RCS priority experiment (tools/rcs/priority_grid.py), per flow, for
both flows together, and per phase of the run: before, during and after the aperiodic1 burst.

    tools/rcs/priority_metrics.py <run folder> <trace folder> --model square|highway \
        --burst 10 20 [--warmup 2] [--range 300] [--out <folder>]

square   the RCS paper's all-or-nothing reception (Rolich et al. 2026, Section III): a packet sent
         in time reaches every other UE unless another transmission used one of its RBs in the
         same slot. phyDebugShort.txt gives every transmission.
highway  what the PHY decoded (ReceivedLog.txt), for every receiver within --range of the sender
         at the generation time (tools/qos/metrics.py attempts).

Outcomes of a packet (square) or of a packet and a receiver (highway):
  ok         received within the PDB (plus the slot it ends in)
  collision  lost to another transmission on the same slot and RBs
  drop       never sent: the MAC dropped it (RCS: no win within the PDB) or sent it after the PDB
  channel    highway only: lost to propagation, half duplex or sensitivity
Packets replaced in the RLC by a newer packet of their flow (superseded) are left out and counted.

PRR, Pcoll and Pdrop are the shares of ok, collision and drop. E[PIR], E[AoI] and V(Ath) come
from the intervals between in-time receptions of one stream (flow, sender, receiver), as in
tools/rcs/metrics.py.

Writes paper.csv (flow x phase), reevaluations.csv (re-evaluation and pre-emption checks and
resource changes per priority and phase), rcs.csv (RCS contention per priority and phase),
cbr_time.csv (mean and max CBR per 100 ms) and metrics.json (a flat copy for the grid summary).
"""
import argparse
import json
import os
import sys

import numpy as np
import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "qos"))
import morev2x_results as mr  # noqa: E402
import importlib.util  # noqa: E402

_spec = importlib.util.spec_from_file_location("qos_metrics", os.path.join(HERE, "..", "qos", "metrics.py"))
qos = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qos)
_spec = importlib.util.spec_from_file_location("rcs_metrics", os.path.join(HERE, "metrics.py"))
rcs_metrics = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rcs_metrics)

RCS_COLUMNS = ["time", "node", "packet", "genMs", "frame", "contention", "outcome", "sc", "delayMs", "group", "priority"]
ALL = "all"
DROP_CAUSES = {"never transmitted", "oversize drop", "intra-UE conflict", "late (decoded after the PDB)"}
COLLISION_CAUSES = {"hidden-node collision", "sensing-visible collision"}


def readme(run_folder):
    lines = open(os.path.join(run_folder, "simREADME.txt"))
    return dict(line.strip().split("=", 1) for line in lines if "=" in line)


def phases(warmup, burst, end):
    return {"before": (warmup, burst[0]), "burst": (burst[0], burst[1]), "after": (burst[1], end), ALL: (warmup, end)}


def square_attempts(run_folder, warmup, end, slot_s):
    """One row per packet: the paper's model, every other UE receives it or none does. With blind
    retransmissions a packet is received when one of its copies is clean and within the PDB."""
    app = qos.read(run_folder, "AppTxLog.txt", qos.APP_COLUMNS)
    flows = qos.read(run_folder, "FlowMap.txt", qos.FLOW_COLUMNS)
    drops = qos.read(run_folder, "MacDropLog.txt", qos.DROP_COLUMNS)
    tx = qos.read(run_folder, "phyDebugShort.txt", mr.TX_COLUMNS)
    tx = tx.drop_duplicates(["packet", "txIndex"]).reset_index(drop=True)
    tx["slot"] = np.round(tx.txTime / slot_s).astype(np.int64)
    tx["rbEnd"] = tx.psschRbStart + tx.psschRbLenTb
    # a transmission collides when another one in its slot overlaps its RBs
    tx["collided"] = False
    for _, group in tx.groupby("slot"):
        if len(group) < 2:
            continue
        start = group.psschRbStart.to_numpy()
        stop = group.rbEnd.to_numpy()
        overlap = (start[:, None] < stop[None, :]) & (start[None, :] < stop[:, None])
        np.fill_diagonal(overlap, False)
        tx.loc[group.index, "collided"] = overlap.any(axis=1)

    app = app[(app.genTime >= warmup) & (app.genTime <= end)].copy()
    superseded = set(drops[drops.reason == "replaced"].packet) if len(drops) else set()
    app["superseded"] = app.packet.isin(superseded)
    pdb = flows.set_index("flow").pdb / 1000.0
    app["pdb"] = app.flow.map(pdb)
    copies = tx.drop(columns=["genTime"]).merge(app[["packet", "genTime", "pdb"]], on="packet", how="inner")
    copies["inBudget"] = (copies.txTime - copies.genTime) <= copies.pdb + slot_s + 1e-9
    clean = copies[copies.inBudget & ~copies.collided].groupby("packet").txTime.min()
    budget = copies[copies.inBudget].groupby("packet").size()
    app["receptionTime"] = app.packet.map(clean) + slot_s
    app["outcome"] = "drop"  # never sent, or sent after the PDB only
    app.loc[app.packet.isin(budget.index), "outcome"] = "collision"  # every copy in time collided
    app.loc[app.receptionTime.notna(), "outcome"] = "ok"
    app["tx"] = app.node
    app["rx"] = -1
    return app[["packet", "flow", "tx", "rx", "genTime", "receptionTime", "outcome", "superseded"]], flows


def highway_attempts(run_folder, trace_folder, warmup, end, slot_ms, max_distance):
    """One row per (packet, receiver within max_distance), from what the PHY decoded."""
    run = qos.compact_run(run_folder)
    app = qos.read(run_folder, "AppTxLog.txt", qos.APP_COLUMNS)
    flows = qos.read(run_folder, "FlowMap.txt", qos.FLOW_COLUMNS)
    drops = qos.read(run_folder, "MacDropLog.txt", qos.DROP_COLUMNS)
    nodes = pd.read_csv(os.path.join(run_folder, "NodeMap.txt"), header=None, names=["node", "role", "group", "position", "flows"])
    positions = qos.Positions(trace_folder, int(nodes.node.min()))
    attempts = qos.attempts_table(run, app, flows, positions, max_distance, warmup, end + 0.2, slot_ms)
    attempts = qos.classify(attempts, run, drops, slot_ms, [])
    attempts["superseded"] = attempts.cause == qos.SUPERSEDED
    attempts["outcome"] = "channel"
    attempts.loc[attempts.cause == "decoded in time", "outcome"] = "ok"
    attempts.loc[attempts.cause.isin(COLLISION_CAUSES), "outcome"] = "collision"
    attempts.loc[attempts.cause.isin(DROP_CAUSES), "outcome"] = "drop"
    return attempts[["packet", "flow", "tx", "rx", "genTime", "receptionTime", "outcome", "superseded"]], flows


def paper_table(attempts, flows, windows):
    """PRR, Pcoll, Pdrop, E[PIR], E[AoI], V(Ath) per flow (and both together) and phase."""
    names = flows.set_index("flow").name.to_dict()
    rows = []
    selections = [(names[f], attempts[attempts.flow == f]) for f in sorted(names)] + [(ALL, attempts)]
    for name, frame in selections:
        for phase, (start, stop) in windows.items():
            generated = frame[(frame.genTime >= start) & (frame.genTime < stop)]
            counted = generated[~generated.superseded]
            row = {"flow": name, "phase": phase, "attempts": int(len(counted)),
                   "superseded": int(generated.superseded.sum())}
            for outcome, column in (("ok", "prr"), ("collision", "pcoll"), ("drop", "pdrop"), ("channel", "pchannel")):
                row[column] = float((counted.outcome == outcome).mean()) if len(counted) else np.nan
            # timeliness: in-time receptions of each stream that end in the phase
            ok = frame[(frame.outcome == "ok") & ~frame.superseded]
            ok = ok[(ok.receptionTime >= start) & (ok.receptionTime < stop)]
            stream = ok.flow.astype(str) + ":" + ok.tx.astype(str) + ":" + ok.rx.astype(str)
            row.update(rcs_metrics.timeliness(ok.receptionTime.to_numpy(), stream.to_numpy()))
            row.pop("flows", None)
            rows.append(row)
    return pd.DataFrame(rows)


def reevaluation_table(run_folder, flows, windows):
    log = qos.read(run_folder, "ReEvaluationsLog.txt", mr.REEVALUATION_COLUMNS)
    names = flows.set_index("priority").name.to_dict()
    rows = []
    for phase, (start, stop) in windows.items():
        part = log[(log.time >= start) & (log.time < stop)] if len(log) else log
        for priority, name in names.items():
            for kind, label in ((0, "reevaluation"), (1, "preemption")):
                sel = part[(part.priority == priority) & (part.kind == kind)] if len(part) else part
                rows.append({"flow": name, "phase": phase, "mechanism": label, "checks": int(len(sel)),
                             "changes": int(sel.reselected.sum()) if len(sel) else 0})
    return pd.DataFrame(rows)


def rcs_table(run_folder, flows, windows):
    log = qos.read(run_folder, "RcsLog.txt", RCS_COLUMNS)
    if not len(log):
        return pd.DataFrame()
    names = flows.set_index("priority").name.to_dict()
    rows = []
    for phase, (start, stop) in windows.items():
        part = log[(log.genMs >= 1000 * start) & (log.genMs < 1000 * stop)]
        for priority, name in names.items():
            sel = part[part.priority == priority]
            final = sel[sel.outcome.isin([0, 2, 3, 5])]
            won = sel[sel.outcome == 0]
            rows.append({"flow": name, "phase": phase, "packets": int(len(final)),
                         "won": int(len(won)), "lost_rounds": int((sel.outcome == 1).sum()),
                         "yielded": int((sel.outcome == 4).sum()),
                         "dropped_no_win": int((sel.outcome == 2).sum()),
                         "dropped_late_win": int((sel.outcome == 3).sum()),
                         "dropped_waiting": int((sel.outcome == 5).sum()),
                         "contentions_per_packet": float(final.contention.mean()) if len(final) else np.nan,
                         "access_delay_ms": float(won.delayMs.mean()) if len(won) else np.nan})
    return pd.DataFrame(rows)


def cbr_series(run_folder, warmup):
    cbr = qos.read(run_folder, "CBR_OutputFile.txt", mr.CBR_COLUMNS)
    cbr = cbr[cbr.time >= warmup]
    if not len(cbr):
        return pd.DataFrame(columns=["time", "mean", "max"])
    return cbr.groupby((cbr.time // 0.1) * 0.1).cbr.agg(["mean", "max"]).reset_index()


def flat(paper, reevals, rcs, cbr, windows):
    out = {}
    for _, r in paper.iterrows():
        for metric in ("prr", "pcoll", "pdrop", "pchannel", "pir_ms", "aoi_ms", "aoi_violation_100",
                       "aoi_violation_200", "aoi_violation_500", "attempts", "superseded"):
            value = r[metric]
            out["%s_%s_%s" % (metric, r.flow, r.phase)] = None if pd.isna(value) else float(value)
    for _, r in reevals.iterrows():
        out["%s_checks_%s_%s" % (r.mechanism, r.flow, r.phase)] = int(r.checks)
        out["%s_changes_%s_%s" % (r.mechanism, r.flow, r.phase)] = int(r.changes)
    for _, r in rcs.iterrows():
        for metric in ("yielded", "dropped_no_win", "dropped_waiting", "contentions_per_packet", "access_delay_ms"):
            value = r[metric]
            out["rcs_%s_%s_%s" % (metric, r.flow, r.phase)] = None if pd.isna(value) else float(value)
    for phase, (start, stop) in windows.items():
        part = cbr[(cbr.time >= start) & (cbr.time < stop)]
        out["cbr_mean_" + phase] = float(part["mean"].mean()) if len(part) else None
        out["cbr_max_" + phase] = float(part["max"].max()) if len(part) else None
    return out


def summarise(run_folder, trace_folder, model, burst, warmup=2.0, max_distance=300.0, out=None):
    info = readme(run_folder)
    sim_time = float(info["simTime"])
    slot_ms = 1.0 / (2 ** int(info["numerology"]))
    # The logs are flushed every savingPeriod: the last block never reaches the files, and packets
    # of the last 200 ms may still be in flight
    end = sim_time - float(info.get("savingPeriod", 0.5)) - 0.2
    windows = phases(warmup, burst, end)
    if model == "square":
        attempts, flows = square_attempts(run_folder, warmup, end, slot_ms / 1000.0)
    else:
        attempts, flows = highway_attempts(run_folder, trace_folder, warmup, end, slot_ms, max_distance)
    paper = paper_table(attempts, flows, windows)
    reevals = reevaluation_table(run_folder, flows, windows)
    rcs = rcs_table(run_folder, flows, windows)
    cbr = cbr_series(run_folder, warmup)
    out = out or os.path.join(run_folder, "metrics")
    os.makedirs(out, exist_ok=True)
    paper.to_csv(os.path.join(out, "paper.csv"), index=False)
    reevals.to_csv(os.path.join(out, "reevaluations.csv"), index=False)
    rcs.to_csv(os.path.join(out, "rcs.csv"), index=False)
    cbr.to_csv(os.path.join(out, "cbr_time.csv"), index=False)
    values = flat(paper, reevals, rcs, cbr, windows)
    json.dump(values, open(os.path.join(out, "metrics.json"), "w"), indent=1)
    return values


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run")
    parser.add_argument("trace")
    parser.add_argument("--model", choices=["square", "highway"], required=True)
    parser.add_argument("--burst", type=float, nargs=2, required=True, metavar=("START", "STOP"))
    parser.add_argument("--warmup", type=float, default=2.0)
    parser.add_argument("--range", type=float, default=300.0, dest="max_distance")
    parser.add_argument("--out")
    args = parser.parse_args(argv)
    summarise(args.run, args.trace, args.model, args.burst, args.warmup, args.max_distance, args.out)


if __name__ == "__main__":
    main()
