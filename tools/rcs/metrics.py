"""Metrics of the RCS paper (Rolich et al., MAIN 2026, arXiv:2607.06103) for one MoReV2X run.

    tools/rcs/metrics.py <results folder> --model ideal|highway --slot-ms 0.5 --pdb 60

ideal   The paper's model: every UE hears every other one and a packet is received
        by all of them if no other transmission used its slot and sub-channel
        (all-or-nothing, half duplex ignored). Read from the transmission logs, so
        the PHY's SINR does not enter. Per packet: PRR (sent alone), Pcoll (sent on a
        shared sub-channel), Pdrop (never sent). Per flow i -> j: the inter-reception
        times Y of the packets sent alone, hence E[PIR] (eq. 11), E[AoI] (eq. 12) and
        the AoI violation V(Ath) (eq. 14) for Ath = 100, 200, 500 ms.
highway The PHY decides: ReceivedLog.txt gives every (packet, receiver) attempt with
        its distance and whether it decoded. PRR per distance bin, counting the
        packets that were never sent as lost for every receiver, and PIR / AoI /
        V(Ath) per (i, j) flow over the receptions within 150, 300 and 500 m.

The packets generated in [--start, last logged transmission - PDB] are counted, so
every one of them had its full delay budget inside the logs.
"""
import argparse
import json
import os
import sys

import numpy as np
import pandas as pd

TX_FILE = ["packet", "genTime", "node", "x", "y", "trafficType", "messageType", "size"]
TX_COLUMNS = ["node", "packet", "genTime", "txIndex", "txTime", "frame", "subframe", "Cresel", "RRI",
              "psschRbStart", "psschRbLen", "psschRbLenTb", "pscchRbStart", "pscchRbLen"]
RX_COLUMNS = ["rxTime", "packetID", "distance", "txID", "rxID", "decoded", "lossType", "txIndex",
              "trigger", "announced", "latency"]
RCS_COLUMNS = ["time", "node", "packet", "genMs", "frame", "contention", "outcome", "sc", "delayMs"]
CONTENTION_COLUMNS = ["frame", "time", "contenders", "winners", "scsUsed", "scsCollided"]
AOI_THRESHOLDS_MS = (100, 200, 500)
DISTANCES_M = (150, 300, 500)


def read(folder, name, columns, **kwargs):
    path = os.path.join(folder, name)
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return pd.DataFrame(columns=columns)
    return pd.read_csv(path, header=None, names=columns, **kwargs)


def timeliness(times, flows):
    """E[PIR], E[AoI] and V(Ath) in ms from reception times (s) grouped by flow.

    times and flows are aligned arrays; a flow needs two receptions (Omega = 2).
    E[PIR] pools every interval (eq. 11 averaged over receivers); E[AoI] and V are
    computed per flow (eqs. 12, 14) and averaged over flows.
    """
    frame = pd.DataFrame({"t": np.asarray(times, dtype=float) * 1000.0, "f": flows}).sort_values(["f", "t"])
    frame["y"] = frame.groupby("f").t.diff()
    y = frame.dropna(subset=["y"])
    out = {"flows": int(y.f.nunique())}
    if y.empty:
        out.update({"pir_ms": np.nan, "aoi_ms": np.nan})
        out.update({"aoi_violation_%d" % a: np.nan for a in AOI_THRESHOLDS_MS})
        return out
    out["pir_ms"] = float(y.y.mean())
    sums = y.groupby("f").y.agg(["sum", lambda s: (s * s).sum()])
    sums.columns = ["sum", "sumsq"]
    out["aoi_ms"] = float((sums.sumsq / (2 * sums["sum"])).mean())
    for a in AOI_THRESHOLDS_MS:
        excess = (y.y - a).clip(lower=0).groupby(y.f).sum()
        out["aoi_violation_%d" % a] = float((excess / sums["sum"]).mean())
    return out


def packets_in_window(folder, start, pdb_ms):
    gen = read(folder, "TxFile.txt", TX_FILE)
    tx = read(folder, "phyDebugShort.txt", TX_COLUMNS)
    end = (tx.txTime.max() if len(tx) else 0.0) - pdb_ms / 1000.0
    gen = gen[(gen.genTime >= start) & (gen.genTime <= end)]
    return gen, tx, end


def ideal(folder, slot_ms, pdb_ms, start, subchannel_rbs):
    gen, tx, end = packets_in_window(folder, start, pdb_ms)
    tx = tx[tx.packet.isin(gen.packet)].copy()
    tx["slot"] = np.rint(tx.txTime * 1000.0 / slot_ms).astype(np.int64)
    # one row per (transmission, sub-channel it occupies)
    first = tx.psschRbStart // subchannel_rbs
    count = np.ceil(tx.psschRbLen / subchannel_rbs).astype(int).clip(lower=1)
    occupied = tx.loc[tx.index.repeat(count)].copy()
    occupied["sc"] = first.loc[occupied.index].values + occupied.groupby(level=0).cumcount().values
    users = occupied.groupby(["slot", "sc"]).packet.transform("size")
    shared = occupied[users > 1].index.unique()
    tx["alone"] = ~tx.index.isin(shared)
    # a packet with several transmissions (blind re-transmissions) needs one alone
    per_packet = tx.groupby("packet").agg(alone=("alone", "max"), sent=("txTime", "min"))
    first_ok = tx[tx.alone].groupby("packet").txTime.min()
    n = len(gen)
    sent = gen.packet.isin(per_packet.index)
    ok = gen.packet.isin(first_ok.index)
    out = {"packets": n, "window_s": [start, end],
           "prr": ok.sum() / n if n else np.nan,
           "pcoll": (sent & ~ok).sum() / n if n else np.nan,
           "pdrop": (~sent).sum() / n if n else np.nan}
    success = gen[ok].merge(first_ok.rename("rxTime"), left_on="packet", right_index=True)
    out["latency_ms"] = float(((success.rxTime - success.genTime) * 1000).mean()) if len(success) else np.nan
    out.update(timeliness(success.rxTime.values, success.node.values))
    return out


def highway(folder, pdb_ms, start, bin_m=25, max_m=1000):
    gen, tx, end = packets_in_window(folder, start, pdb_ms)
    n = len(gen)
    sent = gen.packet.isin(tx.packet)
    pdrop = (~sent).sum() / n if n else np.nan
    out = {"packets": n, "window_s": [start, end], "pdrop": pdrop}
    keep = set(gen.packet)
    bins = {}
    near = []
    path = os.path.join(folder, "ReceivedLog.txt")
    usecols = [0, 1, 2, 3, 4, 5]
    for chunk in pd.read_csv(path, header=None, names=RX_COLUMNS[:6], usecols=usecols, chunksize=5_000_000):
        chunk = chunk[chunk.packetID.isin(keep) & (chunk.distance < max(max_m, max(DISTANCES_M)))]
        # one row per (packet, receiver): received if any copy decoded
        chunk = chunk.sort_values("decoded", ascending=False).drop_duplicates(["packetID", "rxID"])
        b = (chunk.distance // bin_m).astype(int)
        agg = chunk.groupby(b).decoded.agg(["sum", "count"])
        for k, row in agg.iterrows():
            s, c = bins.get(k, (0, 0))
            bins[k] = (s + row["sum"], c + row["count"])
        near.append(chunk[(chunk.decoded == 1) & (chunk.distance <= max(DISTANCES_M))]
                    [["rxTime", "txID", "rxID", "distance"]])
    # a copy decoded twice (blind re-transmission across chunks) is rare; keep the first
    curve = pd.DataFrame([(k * bin_m + bin_m / 2, s / c, c) for k, (s, c) in sorted(bins.items())],
                         columns=["distance", "prr_sent", "attempts"])
    curve["prr"] = curve.prr_sent * (1 - pdrop)
    out["prr_by_distance"] = curve[curve.distance < max_m].round(6).to_dict(orient="list")
    near = pd.concat(near) if near else pd.DataFrame(columns=["rxTime", "txID", "rxID", "distance"])
    for d in DISTANCES_M:
        sub = near[near.distance <= d]
        flows = sub.txID.astype(np.int64) * 100000 + sub.rxID.astype(np.int64)
        for key, value in timeliness(sub.rxTime.values, flows.values).items():
            out["%s_%dm" % (key, d)] = value
        attempts = curve[curve.distance <= d]
        out["prr_%dm" % d] = float((attempts.prr * attempts.attempts).sum() / attempts.attempts.sum()) if len(attempts) else np.nan
    return out


def rcs_stats(folder, start):
    log = read(folder, "RcsLog.txt", RCS_COLUMNS)
    if log.empty:
        return {}
    log = log[log.genMs >= start * 1000]
    final = log[log.outcome != 1]
    won = final[final.outcome == 0]
    contention = read(folder, "RcsContentionLog.txt", CONTENTION_COLUMNS)
    contention = contention[contention.time >= start]
    return {"rcs_contentions_per_packet": float(final.contention.mean()),
            "rcs_access_delay_ms": float(won.delayMs.mean()),
            "rcs_drop_no_win": float((final.outcome == 2).mean()),
            "rcs_drop_late_win": float((final.outcome == 3).mean()),
            "rcs_contenders_per_frame": float(contention.contenders.mean()),
            "rcs_winners_per_frame": float(contention.winners.mean()),
            "rcs_scs_collided_per_frame": float(contention.scsCollided.mean())}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder")
    parser.add_argument("--model", choices=["ideal", "highway"], required=True)
    parser.add_argument("--slot-ms", type=float, required=True)
    parser.add_argument("--pdb", type=float, default=60.0)
    parser.add_argument("--start", type=float, default=1.0, help="ignore packets generated before (s)")
    parser.add_argument("--subchannel", type=int, default=25, help="sub-channel size (RBs)")
    parser.add_argument("--out", help="write the metrics to this JSON file")
    args = parser.parse_args(argv)
    if args.model == "ideal":
        out = ideal(args.folder, args.slot_ms, args.pdb, args.start, args.subchannel)
    else:
        out = highway(args.folder, args.pdb, args.start)
    out.update(rcs_stats(args.folder, args.start))
    out = {k: (None if isinstance(v, float) and np.isnan(v) else v) for k, v in out.items()}
    text = json.dumps(out, indent=1, default=float)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
    else:
        print(text)


if __name__ == "__main__":
    sys.exit(main())
