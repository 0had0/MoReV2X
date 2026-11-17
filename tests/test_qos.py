"""End-to-end checks of the QOS scenarios: traces, the QOS program, impairments and metrics.
Run with tools/gate.sh, or .venv/bin/python -m unittest tests.test_qos -v after sourcing .env."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

import numpy as np
import pandas as pd
import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tools", "qos"))
import metrics  # noqa: E402
import sweep  # noqa: E402

BUILD = os.path.join(ROOT, "build", "optimized")
QOS = os.path.join(BUILD, "scratch", "QOS")
PYTHON = sys.executable
TRACES = {}


def trace(scenario, **options):
    """A small trace, made once per test session."""
    key = (scenario, tuple(sorted(options.items())))
    if key not in TRACES:
        folder = tempfile.mkdtemp(prefix="qos-trace-")
        command = [PYTHON, os.path.join(ROOT, "tools", "qos", "make_traces.py"), scenario, "--out", folder,
                   "--duration", "6", "--warmup", "100"]
        for name, value in options.items():
            command += ["--" + name.replace("_", "-"), str(value)]
        subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        TRACES[key] = folder
    return TRACES[key]


def flows_file(folder, names, overrides=None):
    config = sweep.load_config(os.path.join(ROOT, "scenarios", "qos", "sweep_full.yaml"))
    rows = [sweep.flow_row(n, config["classes"][n], overrides or {}) for n in names]
    path = os.path.join(folder, "flows.csv")
    pd.DataFrame(rows, columns=sweep.FLOW_HEADER).to_csv(path, index=False)
    return path


class QosRun(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.mkdtemp(prefix="qos-run-")

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def qos(self, trace_folder, names, *arguments, overrides=None, sim_time=4):
        flows = flows_file(self.work, names, overrides)
        out = os.path.join(self.work, "logs") + "/"
        command = [QOS, "--trace=" + trace_folder, "--flows=" + flows, "--out=" + out, "--simTime=%g" % sim_time,
                   "--Numerology=2", "--ChannelBW=20", "--SubChannel=12", "--mcs=14", "--SavingPeriod=0.5",
                   "--ns3::NrV2XSpectrumPhy::MetricsMaxDistance=1000"] + list(arguments)
        env = dict(os.environ, DYLD_LIBRARY_PATH=BUILD, LD_LIBRARY_PATH=BUILD)
        done = subprocess.run(command, cwd=self.work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, timeout=900)
        if done.returncode != 0:
            raise AssertionError("QOS failed:\n" + done.stdout[-3000:])
        self.stdout = done.stdout
        return out

    def summary(self, out, trace_folder):
        return metrics.summarise(out, trace_folder, 1.0, 1000.0, os.path.join(self.work, "metrics"))


class Traces(unittest.TestCase):
    def test_highway_platoons_are_tight_and_in_the_right_lane(self):
        folder = trace("highway_platoon", length=1000, platoons=2, platoon_size=4, density="low")
        t = pd.read_csv(os.path.join(folder, "trace.csv")).merge(pd.read_csv(os.path.join(folder, "roles.csv")), on="node_id")
        platoon = t[t.role == "platoon"]
        self.assertEqual(platoon.node_id.nunique(), 8)
        first = platoon[platoon.time == platoon.time.min()].sort_values(["group", "position"])
        gaps = first.groupby("group").x.apply(lambda x: x.diff().abs().dropna().median())
        self.assertTrue((gaps < 30).all(), gaps)

    def test_urban_has_buildings_rsus_and_vrus(self):
        folder = trace("urban_intersection", density="low")
        roles = pd.read_csv(os.path.join(folder, "roles.csv"))
        self.assertEqual((roles.role == "rsu").sum(), 4)
        self.assertGreater(roles.role.isin(["cyclist", "pedestrian"]).sum(), 0)
        self.assertEqual(len(pd.read_csv(os.path.join(folder, "buildings.csv"))), 9)

    def test_same_seed_same_trace(self):
        a = trace("highway_cpm", length=600, density="low", seed=3)
        b = tempfile.mkdtemp(prefix="qos-trace-")
        subprocess.run([PYTHON, os.path.join(ROOT, "tools", "qos", "make_traces.py"), "highway_cpm", "--out", b,
                        "--duration", "6", "--warmup", "100", "--length", "600", "--density", "low", "--seed", "3"],
                       check=True, stdout=subprocess.PIPE)
        self.assertEqual(open(os.path.join(a, "trace.csv")).read(), open(os.path.join(b, "trace.csv")).read())
        shutil.rmtree(b)


class QosScenario(QosRun):
    CLASSES = ["platooning", "cam", "cooperative_perception", "crash_mitigation"]

    def test_every_class_has_its_priority_and_is_received(self):
        folder = trace("highway_platoon", length=1000, platoons=2, platoon_size=4, density="low")
        out = self.qos(folder, self.CLASSES, overrides={"crash_mitigation": {"event_rate": 2.0}})
        ssps = pd.read_csv(os.path.join(out, "SSPSlog.txt"), header=None)
        self.assertTrue({1, 3, 5, 6} <= set(ssps[23]), set(ssps[23]))
        summary = self.summary(out, folder).set_index("name")
        for name in self.CLASSES:
            self.assertGreater(summary.at[name, "attempts"], 0, name)
            self.assertGreater(summary.at[name, "pdr"], 0.5, name)
        causes = summary[[c for c in summary.columns if c.startswith("cause_")]].sum(axis=1)
        self.assertTrue((causes == summary.attempts).all())
        self.assertIn("slot payload limit", self.stdout)

    def test_one_pssch_per_slot_and_dynamic_classes(self):
        folder = trace("highway_platoon", length=1000, platoons=2, platoon_size=4, density="low")
        out = self.qos(folder, self.CLASSES, overrides={"crash_mitigation": {"event_rate": 2.0}})
        tx = pd.read_csv(os.path.join(out, "phyDebugShort.txt"), header=None)
        self.assertEqual(tx.groupby([0, 4]).size().max(), 1)
        ssps = pd.read_csv(os.path.join(out, "SSPSlog.txt"), header=None)
        self.assertTrue((ssps[ssps[23] == 1][9] <= 1).all(), "crash mitigation selects per packet")
        crash = ssps[(ssps[23] == 1) & (ssps[14] == 1)][13]
        self.assertEqual(crash.max(), 3, "crash mitigation sends 3 copies (PER 1e-5)")
        # within a 3 ms PDB the 20 % rule can leave candidates on two slots only
        self.assertGreater((crash == 3).mean(), 0.8)
        self.assertEqual(set(ssps[ssps[23] == 5][13]), {2}, "CAM sends 2 copies (PER 1e-4)")
        self.assertEqual(set(ssps[ssps[23] == 6][13]), {1}, "CPM sends 1 copy (PER 1e-2)")

    def test_preemption_only_takes_from_lower_priorities(self):
        folder = trace("highway_cpm", length=600, density="high")
        out = self.qos(folder, ["platooning", "cam", "cooperative_perception"], "--ReEvaluation=1", "--Preemption=1",
                       overrides={"platooning": {"roles": ["car", "truck"], "share": 0.3},
                                  "cooperative_perception": {"share": 1.0}})
        log = pd.read_csv(os.path.join(out, "ReEvaluationsLog.txt"), header=None)
        preempted = log[(log[13] == 1) & (log[12] == 1)]
        self.assertGreater(len(preempted), 0)
        self.assertNotIn(3, set(preempted[14]), "nothing above platooning (3) is an SPS class here")

    def test_loss_bursts_are_logged(self):
        folder = trace("highway_platoon", length=1000, platoons=2, platoon_size=4, density="low")
        out = self.qos(folder, ["platooning", "cam"], "--BurstLoss=platoon;0.3;0.1")
        rx = pd.read_csv(os.path.join(out, "ReceivedLog.txt"), header=None)
        self.assertGreater((rx[6] == 4).sum(), 0)
        nodes = pd.read_csv(os.path.join(out, "NodeMap.txt"), header=None)
        platoon = set(nodes[nodes[1] == "platoon"][0])
        self.assertTrue(set(rx[rx[6] == 4][4]) <= platoon, "only the platoon trucks lose bursts")

    def test_jammer_raises_losses(self):
        folder = trace("highway_platoon", length=1000, platoons=2, platoon_size=4, density="low")
        clean = self.summary(self.qos(folder, ["cam"]), folder)
        clean_pdr = clean.pdr.iloc[0]
        shutil.rmtree(os.path.join(self.work, "metrics"))
        jammed = self.summary(self.qos(folder, ["cam"], "--Jammer=500,20,30,1.0,10,0.5,4"), folder)
        self.assertLess(jammed.pdr.iloc[0], clean_pdr - 0.05)

    def test_stack_latency_delays_packets(self):
        # 5 ms between generation and the sidelink: nothing can arrive sooner than that. The median
        # need not grow, SPS places the reserved slot relative to the delayed arrival
        folder = trace("highway_platoon", length=1000, platoons=2, platoon_size=4, density="low")
        fast = self.qos(folder, ["platooning"])
        rx = pd.read_csv(os.path.join(fast, "ReceivedLog.txt"), header=None)
        self.assertLess(rx[rx[5] == 1][10].min(), 0.005)
        slow = self.qos(folder, ["platooning"], overrides={"platooning": {"stack_delay_ms": 5}})
        rx = pd.read_csv(os.path.join(slow, "ReceivedLog.txt"), header=None)
        self.assertGreaterEqual(rx[rx[5] == 1][10].min(), 0.005 - 1e-6)

    def test_urban_buildings_cut_links(self):
        folder = trace("urban_intersection", density="low")
        open_air = self.summary(self.qos(folder, ["cam", "vam"]), folder)
        shutil.rmtree(os.path.join(self.work, "metrics"))
        urban = self.summary(self.qos(folder, ["cam", "vam"], "--Urban=1"), folder)
        self.assertLess(urban.set_index("name").pdr["cam"], open_air.set_index("name").pdr["cam"])


class GroupcastTraffic(QosRun):
    """The traffic models of groupcast-adaptation-ns3 (scenarios/qos/groupcast_traffic.yaml)."""

    def test_packets_follow_each_model(self):
        classes = yaml.safe_load(open(os.path.join(ROOT, "scenarios", "qos", "groupcast_traffic.yaml")))["classes"]
        names = list(classes)
        rows = [sweep.flow_row(n, classes[n], {n: {"share": 0.3}}) for n in names]
        flows = os.path.join(self.work, "groupcast.csv")
        pd.DataFrame(rows, columns=sweep.FLOW_HEADER).to_csv(flows, index=False)
        folder = trace("highway_platoon", length=1000, platoons=2, platoon_size=4, density="low")
        out = os.path.join(self.work, "logs") + "/"
        # 20 MHz at 15 kHz with 25-RB subchannels: one slot carries the 2000 B of aperiodic1
        command = [QOS, "--trace=" + folder, "--flows=" + flows, "--out=" + out, "--simTime=4",
                   "--Numerology=0", "--ChannelBW=20", "--SubChannel=25", "--mcs=14", "--SavingPeriod=0.5",
                   "--ns3::NrV2XSpectrumPhy::MetricsMaxDistance=1000"]
        env = dict(os.environ, DYLD_LIBRARY_PATH=BUILD, LD_LIBRARY_PATH=BUILD)
        done = subprocess.run(command, cwd=self.work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, timeout=900)
        self.assertEqual(done.returncode, 0, done.stdout[-3000:])
        self.assertNotIn("dropped as oversize", done.stdout)
        app = pd.read_csv(os.path.join(out, "AppTxLog.txt"), header=None, names=metrics.APP_COLUMNS)
        app = app.sort_values(["flow", "node", "genTime"])
        app["gap"] = app.groupby(["flow", "node"]).genTime.diff()
        flow = {name: app[app.flow == i] for i, name in enumerate(names)}
        for name in names:
            self.assertGreater(len(flow[name]), 20, name)

        def whole_periods(frame, period_ms):
            # AppTxLog keeps 6 significant digits: a few microseconds of rounding
            gap_ms = frame.gap.dropna() * 1000.0
            periods = np.round(gap_ms / period_ms)
            return bool((np.abs(gap_ms - periods * period_ms) < 0.01).all() and (periods >= 1).all())

        p1 = flow["gc_periodic1"]
        self.assertEqual(set(p1["size"]), {300, 190})
        self.assertTrue(whole_periods(p1, 100))
        for _, sizes in p1.groupby("node")["size"]:
            big = np.flatnonzero(sizes.to_numpy() == 300)
            self.assertTrue((np.diff(big) == 5).all(), "300 B every fifth packet")
        p2 = flow["gc_periodic2"]
        self.assertEqual(set(p2["size"]), {800, 1200})
        self.assertTrue(whole_periods(p2, 10))
        self.assertAlmostEqual((p2["size"] == 1200).mean(), 0.2, delta=0.05)
        self.assertTrue(whole_periods(flow["gc_periodic4"], 20))
        p5 = flow["gc_periodic5"]
        self.assertEqual(set(p5["size"]), {1200})
        self.assertTrue(whole_periods(p5, 50))
        a1 = flow["gc_aperiodic1"]
        self.assertTrue(set(a1["size"]) <= set(range(200, 2001, 200)))
        self.assertGreater(a1["size"].nunique(), 5)
        self.assertTrue(whole_periods(a1, 1), "the exponential part is in whole ms")
        self.assertGreaterEqual(a1.gap.min(), 0.05 - 1e-5)

        summary = self.summary(out, folder).set_index("name")
        for name in names:
            self.assertGreater(summary.at[name, "pdr"], 0, name)

    def test_refused_models(self):
        rows = [sweep.flow_row("big", {"traffic": "periodic3", "pqi": 0, "priority": 1, "pdb_ms": 30, "per": 0,
                                       "roles": ["car"], "rri_ms": 30}, {})]
        flows = os.path.join(self.work, "big.csv")
        pd.DataFrame(rows, columns=sweep.FLOW_HEADER).to_csv(flows, index=False)
        folder = trace("highway_platoon", length=1000, platoons=2, platoon_size=4, density="low")
        env = dict(os.environ, DYLD_LIBRARY_PATH=BUILD, LD_LIBRARY_PATH=BUILD)
        done = subprocess.run([QOS, "--trace=" + folder, "--flows=" + flows, "--out=" + self.work + "/x/", "--simTime=1"],
                              cwd=self.work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              timeout=300)
        self.assertNotEqual(done.returncode, 0)
        self.assertIn("larger than a transport block", done.stdout)


class RcsPriority(QosRun):
    """The RCS priority experiment (tools/rcs/priority_grid.py) on a small static square."""

    def run_qos(self, square, flows, name, *arguments):
        out = os.path.join(self.work, name) + "/"
        command = [QOS, "--trace=" + square, "--flows=" + flows, "--out=" + out, "--simTime=6", "--Numerology=1",
                   "--ChannelBW=20", "--SubChannel=25", "--mcs=13", "--Ideal=1", "--RxLog=0"] + list(arguments)
        env = dict(os.environ, DYLD_LIBRARY_PATH=BUILD, LD_LIBRARY_PATH=BUILD)
        done = subprocess.run(command, cwd=self.work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, timeout=900)
        self.assertEqual(done.returncode, 0, done.stdout[-3000:])
        return out

    def test_two_flows_same_traffic_and_urgent_first(self):
        square = os.path.join(self.work, "square")
        subprocess.run([PYTHON, os.path.join(ROOT, "tools", "rcs", "static_square.py"), "--vehicles", "40",
                        "--duration", "7", "--out", square], check=True)
        classes = yaml.safe_load(open(os.path.join(ROOT, "scenarios", "rcs", "priority_classes.yaml")))["classes"]
        overrides = {"aperiodic1": {"traffic": "aperiodic1:400", "start_s": 2, "stop_s": 4}}
        flows = os.path.join(self.work, "flows.csv")
        pd.DataFrame([sweep.flow_row(n, classes[n], overrides) for n in classes],
                     columns=sweep.FLOW_HEADER).to_csv(flows, index=False)
        sps = self.run_qos(square, flows, "sps", "--ReEvaluation=1", "--Preemption=1")
        rcs = self.run_qos(square, flows, "rcs", "--Rcs=1", "--RcsGroups=1", "--RcsChoice=skewed")

        app = {k: pd.read_csv(os.path.join(f, "AppTxLog.txt"), header=None, names=metrics.APP_COLUMNS)
               for k, f in (("sps", sps), ("rcs", rcs))}
        columns = ["node", "flow", "genTime", "size"]
        pd.testing.assert_frame_equal(app["sps"][columns].reset_index(drop=True), app["rcs"][columns].reset_index(drop=True),
                                      obj="the same traffic under both schedulers")
        burst = app["rcs"][app["rcs"].flow == 1]
        self.assertTrue(burst.genTime.between(2, 4).all(), "aperiodic1 only in its window")
        self.assertEqual(set(burst["size"]), {200, 400})
        self.assertAlmostEqual(burst.node.nunique() / 40, 0.7, delta=0.2)

        log = pd.read_csv(os.path.join(rcs, "RcsLog.txt"), header=None, names=[
            "time", "node", "packet", "genMs", "frame", "contention", "outcome", "sc", "delayMs", "group", "priority"])
        self.assertGreater((log.outcome == 4).sum(), 0, "periodic1 gives the contention to aperiodic1")
        self.assertEqual(set(log[log.outcome == 4].priority), {5})
        self.assertTrue((log[log.outcome == 0].delayMs <= 100).all())

        for name, folder in (("sps", sps), ("rcs", rcs)):
            subprocess.run([PYTHON, os.path.join(ROOT, "tools", "rcs", "priority_metrics.py"), folder, square,
                            "--model", "square", "--burst", "2", "4", "--warmup", "1"], check=True)
            paper = pd.read_csv(os.path.join(folder, "metrics", "paper.csv")).set_index(["flow", "phase"])
            self.assertGreater(paper.at[("periodic1", "all"), "prr"], 0.5, name)
            self.assertGreater(paper.at[("aperiodic1", "burst"), "prr"], 0.5, name)
            total = paper[["prr", "pcoll", "pdrop", "pchannel"]].loc[("all", "all")].sum()
            self.assertAlmostEqual(total, 1.0, places=6)
            values = json.load(open(os.path.join(folder, "metrics", "metrics.json")))
            self.assertGreater(values["cbr_mean_burst"], values["cbr_mean_before"], name + ": CBR rises in the burst")
        reevals = pd.read_csv(os.path.join(sps, "metrics", "reevaluations.csv"))
        self.assertGreater(reevals[reevals.mechanism == "preemption"].checks.sum(), 0)


class SweepRunner(unittest.TestCase):
    def test_expand_and_flows(self):
        config = sweep.load_config(os.path.join(ROOT, "scenarios", "qos", "sweep_full.yaml"))
        runs = sweep.expand(config)
        self.assertEqual(len({r["id"] for r in runs}), len(runs))
        self.assertEqual(set(r["policy"] for r in runs), {"off", "reeval", "reeval_preempt"})
        smoke = sweep.expand(sweep.load_config(os.path.join(ROOT, "scenarios", "qos", "sweep_smoke.yaml")))
        self.assertEqual({r["scenario"] for r in smoke}, {r["scenario"] for r in runs})
        self.assertEqual({r["condition"] for r in smoke}, {r["condition"] for r in runs})
        self.assertEqual({r["variant"] for r in smoke} - {"default"}, {r["variant"] for r in runs} - {"default"})

    def test_traffic_classes_set_their_own_arrivals(self):
        row = sweep.flow_row("p2", {"traffic": "periodic2", "pqi": 0, "priority": 1, "pdb_ms": 10, "per": 0,
                                    "roles": ["car"], "rri_ms": 10}, {})
        self.assertEqual(len(row), len(sweep.FLOW_HEADER))
        self.assertEqual(row[sweep.FLOW_HEADER.index("traffic")], "periodic2")
        with self.assertRaises(ValueError):
            sweep.flow_row("p2", {"traffic": "periodic2", "sizes": [800], "pqi": 0, "priority": 1, "pdb_ms": 10,
                                  "per": 0, "roles": ["car"], "rri_ms": 10}, {})


if __name__ == "__main__":
    unittest.main()
