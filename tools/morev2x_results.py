"""Read the MoReV2X output logs and compute the usual sidelink KPIs.

    import morev2x_results as mr
    run = mr.Run("results/Periodic_Dynamic0_...")
    mr.pdr_by_distance(run.rx)
"""
import os
import subprocess

import numpy as np
import pandas as pd

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SSPS_COLUMNS = ["node", "time", "frame", "subframe", "iterations", "rsrpThreshold", "nCSRfinal",
                "nCSRpastTx", "nCSRinitial", "Cresel", "RRI", "RRIslots", "PDB", "txPerTB",
                "grantIndex", "trigger", "nextFrame", "nextSubframe", "rbStart", "rbLen",
                "reEvalEnabled", "reEvalFrame", "reEvalSubframe"]
RX_COLUMNS = ["rxTime", "packetID", "distance", "txID", "rxID", "decoded", "lossType", "txIndex",
              "trigger", "announced", "latency"]
TX_COLUMNS = ["node", "packet", "genTime", "txIndex", "txTime", "frame", "subframe", "Cresel", "RRI",
              "psschRbStart", "psschRbLen", "psschRbLenTb", "pscchRbStart", "pscchRbLen"]
CBR_COLUMNS = ["node", "cbr", "time"]
RESERVATION_COLUMNS = ["node", "time", "unusedSubchannelRatio", "unusedReservations", "reservations",
                       "latencyReselections", "sizeReselections", "counterReselections",
                       "transmissions"]
REEVALUATION_COLUMNS = ["freshGrant", "node", "time", "packet", "checkedTxIndex", "frame", "subframe",
                        "lastFrame", "lastSubframe", "csr", "checkFrame", "checkSubframe", "reselected"]
POSITION_COLUMNS = ["time", "node", "x", "y", "z", "vx", "vy", "vz", "traffic", "inside"]
SCHEDULER_COLUMNS = ["node", "dynamic", "retx", "reEvaluation", "rsrpThreshold", "mcs", "pkeep", "adaptive"]

LOSS_NAMES = {0: "below sensitivity", 1: "half duplex", 2: "propagation", 3: "collision", 9: "decoded"}
TRIGGER_NAMES = {0: "counter", 1: "latency and size", 2: "latency", 3: "size", 4: "re-evaluation"}

FILES = {
    "ssps": ("SSPSlog.txt", SSPS_COLUMNS),
    "rx": ("ReceivedLog.txt", RX_COLUMNS),
    "tx": ("phyDebugShort.txt", TX_COLUMNS),
    "cbr": ("CBR_OutputFile.txt", CBR_COLUMNS),
    "reservations": ("ReservationsLog.txt", RESERVATION_COLUMNS),
    "reevaluations": ("ReEvaluationsLog.txt", REEVALUATION_COLUMNS),
    "positions": ("posFile.txt", POSITION_COLUMNS),
    "schedulers": ("SchedulerMap.txt", SCHEDULER_COLUMNS),
}


class Run:
    """One results folder. Each log is parsed on first use and returns a DataFrame."""

    def __init__(self, folder):
        self.folder = folder
        self._cache = {}

    def __getattr__(self, name):
        if name not in FILES:
            raise AttributeError(name)
        if name not in self._cache:
            filename, columns = FILES[name]
            path = os.path.join(self.folder, filename)
            if os.path.exists(path) and os.path.getsize(path) > 0:
                self._cache[name] = pd.read_csv(path, header=None, names=columns)
            else:
                self._cache[name] = pd.DataFrame(columns=columns)
        return self._cache[name]


def simulate(tag, *arguments, workdir=None):
    """Runs HIGHWAY with the arguments and returns a Run for its output folder.

    Reuses a finished run with the same tag, so re-executing a notebook only pays
    for what changed. Delete results/notebooks/ to force everything to run again.
    """
    workdir = workdir or os.path.join(ROOT, "results", "notebooks", tag)
    os.makedirs(workdir, exist_ok=True)
    marker = os.path.join(workdir, "stdout.log")
    arguments = [str(a) for a in arguments]
    identity = " ".join(arguments)
    identity_file = os.path.join(workdir, "arguments.txt")
    finished = (os.path.exists(marker) and os.path.exists(identity_file)
                and open(identity_file).read() == identity)
    if not finished:
        environment = dict(os.environ, DYLD_LIBRARY_PATH=os.path.join(ROOT, "build", "lib"))
        with open(marker, "w") as log:
            subprocess.run([os.path.join(ROOT, "build", "scratch", "ns3.42-HIGHWAY-optimized")] + arguments,
                           cwd=workdir, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
        with open(identity_file, "w") as out:
            out.write(identity)
    results = os.path.join(workdir, "results")
    return Run(os.path.join(results, sorted(os.listdir(results))[-1]))


def first_copies(rx):
    """Keeps one row per (packet, receiver), counting a packet as received if any copy decoded."""
    return (rx.sort_values("decoded", ascending=False)
              .drop_duplicates(["packetID", "rxID"]))


def pdr_by_distance(rx, bin_m=50, max_m=1000):
    """Packet delivery ratio per Tx-Rx distance bin, after combining blind re-transmissions."""
    frame = first_copies(rx)
    frame = frame[frame.distance < max_m]
    bins = (frame.distance // bin_m) * bin_m + bin_m / 2
    return frame.groupby(bins).decoded.mean().rename("pdr")


def loss_breakdown(rx, bin_m=50, max_m=1000):
    """Share of each reception outcome per distance bin, one column per outcome."""
    frame = rx[rx.distance < max_m]
    bins = ((frame.distance // bin_m) * bin_m + bin_m / 2).rename("distance")
    table = pd.crosstab(bins, frame.lossType.map(LOSS_NAMES), normalize="index")
    return table


def latency_summary(rx):
    """Latency in ms of the decoded packets."""
    decoded = rx[rx.decoded == 1].latency * 1000
    return decoded.describe(percentiles=[0.5, 0.95, 0.99])


def inter_reception_times(rx, max_distance=300):
    """Time in ms between consecutive decoded packets of the same Tx-Rx pair."""
    frame = rx[(rx.decoded == 1) & (rx.distance < max_distance)].sort_values("rxTime")
    gaps = frame.groupby(["txID", "rxID"]).rxTime.diff().dropna() * 1000
    return gaps[gaps > 0]


def age_of_information(rx, max_distance=300):
    """Time-average AoI and peak AoI in ms for each Tx-Rx pair.

    The sawtooth between two updates i and i+1 of a pair, Y apart in time and with
    delay T of update i, has area Y*T + Y^2/2. rxTime has 10 ms resolution, latency
    has 0.1 ms, so generation time is rxTime - latency.
    """
    frame = rx[(rx.decoded == 1) & (rx.distance < max_distance)].sort_values("rxTime")
    rows = []
    for pair, group in frame.groupby(["txID", "rxID"]):
        arrival = group.rxTime.to_numpy()
        delay = group.latency.to_numpy()
        gap = np.diff(arrival)
        if len(gap) < 2 or gap.sum() <= 0:
            continue
        area = np.sum(gap * delay[:-1] + gap ** 2 / 2)
        peak = gap + delay[:-1]
        rows.append({"txID": pair[0], "rxID": pair[1], "aoi": 1000 * area / gap.sum(),
                     "paoi": 1000 * peak.mean()})
    return pd.DataFrame(rows)


def cbr_over_time(cbr, step=0.5):
    """Mean CBR across UEs in steps of `step` seconds."""
    return cbr.groupby((cbr.time // step) * step).cbr.mean()


def summary(run, max_distance=300):
    """One row of headline numbers for a run."""
    rx = first_copies(run.rx)
    near = rx[rx.distance < max_distance]
    return pd.Series({
        "PDR near": near.decoded.mean(),
        "collision share": (run.rx.lossType == 3).mean(),
        "CBR": run.cbr.cbr.mean(),
        "latency p95 [ms]": run.rx[run.rx.decoded == 1].latency.quantile(0.95) * 1000,
        "selections": run.ssps.groupby(["node", "time"]).ngroups,
        "transmissions": len(run.tx),
    })


SERIES_COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
NEUTRAL = "#8a8985"


def apply_chart_style():
    """Thin marks and a recessive grid. One fixed hue order for every notebook."""
    import matplotlib.pyplot as plt
    from cycler import cycler
    plt.rcParams.update({
        "figure.figsize": (7.2, 3.6), "figure.dpi": 120, "axes.spines.top": False,
        "axes.spines.right": False, "axes.grid": True, "grid.color": "#e6e5e0", "grid.linewidth": 0.8,
        "axes.axisbelow": True, "axes.edgecolor": "#b8b7b0", "axes.titlelocation": "left",
        "axes.titlesize": 11, "axes.labelcolor": "#52514e", "xtick.color": "#52514e",
        "ytick.color": "#52514e", "lines.linewidth": 2, "lines.markersize": 5,
        "legend.frameon": False, "font.size": 9.5,
        "axes.prop_cycle": cycler(color=SERIES_COLORS),
    })
