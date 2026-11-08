"""Loading and plotting helpers for the QOS summaries, used by notebooks 11 and 12.

    import plots
    s = plots.Sweep()               # results/qos/$QOS_SWEEP, default qos_full, else qos_smoke
    s.flows, s.by_distance, s.causes, s.histograms, s.cbr_time, s.runs
"""
import os

import numpy as np
import pandas as pd

import morev2x_results as mr

ROOT = mr.ROOT
POLICY_ORDER = ["off", "reeval", "reeval_preempt"]
POLICY_LABELS = {"off": "off", "reeval": "re-evaluation", "reeval_preempt": "re-evaluation + pre-emption"}
CLASS_ORDER = ["crash_mitigation", "emergency_trajectory", "platooning", "advanced_driving", "cam", "vam",
               "cooperative_perception"]
CAUSE_ORDER = ["late (decoded after the PDB)", "oversize drop", "intra-UE conflict", "never transmitted", "half duplex",
               "below sensitivity", "propagation", "hidden-node collision", "sensing-visible collision", "injected burst"]
CAUSE_COLORS = dict(zip(CAUSE_ORDER, ["#eda100", "#8a8985", "#c4c3bd", "#5d5c58", "#7b5ea7", "#b8b7b0", "#1baf7a",
                                      "#eb6834", "#2a78d6", "#d43d51"]))


class Sweep:
    def __init__(self, name=None):
        name = name or os.environ.get("QOS_SWEEP")
        if name is None:
            name = "qos_full" if os.path.exists(os.path.join(ROOT, "results", "qos", "qos_full", "summary")) else "qos_smoke"
        self.name = name
        self.folder = os.path.join(ROOT, "results", "qos", name, "summary")
        if not os.path.exists(os.path.join(self.folder, "flows.csv")):
            raise FileNotFoundError("No summary in %s: run the sweep, or download it with tools/qos/remote/download.sh"
                                    % self.folder)
        for table in ("flows", "by_distance", "causes", "histograms", "cbr_time", "runs"):
            path = os.path.join(self.folder, table + ".csv")
            setattr(self, table, pd.read_csv(path) if os.path.exists(path) else pd.DataFrame())
        names = self.flows[["scenario", "flow", "name"]].drop_duplicates()
        for table in ("by_distance", "causes", "histograms"):
            frame = getattr(self, table)
            if len(frame) and "name" not in frame:
                setattr(self, table, frame.merge(names, on=["scenario", "flow"], how="left"))
        self.smoke = name == "qos_smoke"

    def note(self):
        seeds = self.runs.seed.nunique() if len(self.runs) else 0
        text = "Sweep %s: %d runs, %d seed(s)." % (self.name, len(self.runs), seeds)
        if self.smoke:
            text += (" SMOKE DATA: 4 s runs on short roads, one seed. The figures show that the pipeline works,"
                     " not the results. Set QOS_SWEEP=qos_full once the full sweep is downloaded.")
        return text


def mean_ci(frame, value, by):
    """Mean over seeds and the 95 % half-width of a t interval (NaN with one seed)."""
    from math import sqrt
    grouped = frame.groupby(by)[value]
    out = grouped.agg(["mean", "std", "count"]).reset_index()
    t = {1: np.nan, 2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 7: 2.45, 8: 2.36, 9: 2.31, 10: 2.26}
    out["ci"] = [t.get(int(n), 1.96) * s / sqrt(n) if n > 1 else np.nan for s, n in zip(out["std"], out["count"])]
    return out


def order_classes(names):
    return [c for c in CLASS_ORDER if c in set(names)] + sorted(set(names) - set(CLASS_ORDER))


def paired_difference(flows, value, baseline="off"):
    """Per scenario, condition, variant, density, class and seed: value(policy) - value(baseline)."""
    keys = ["scenario", "density", "condition", "variant", "seed", "name"]
    base = flows[flows.policy == baseline].set_index(keys)[value]
    rows = []
    for policy in [p for p in POLICY_ORDER if p != baseline]:
        other = flows[flows.policy == policy].set_index(keys)[value]
        diff = (other - base).dropna().rename("delta").reset_index()
        diff["policy"] = policy
        rows.append(diff)
    return pd.concat(rows, ignore_index=True) if rows else pd.DataFrame()
