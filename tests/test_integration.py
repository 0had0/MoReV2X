"""End-to-end checks on real simulator runs. Run with: tools/gate.sh, or
.venv/bin/python -m unittest discover -s tests -v after sourcing .env."""
import os
import shutil
import subprocess
import tempfile
import unittest

import pandas as pd

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "optimized")
HIGHWAY = os.path.join(BUILD, "scratch", "HIGHWAY")
SUMO_CFG = os.path.join(ROOT, "scenarios", "sumo", "highway_2km", "highway.sumocfg")
TIMEOUT = 600

SSPS_COLUMNS = ["node", "time", "frame", "subframe", "iterations", "rsrpThreshold", "nCSRfinal",
                "nCSRpastTx", "nCSRinitial", "Cresel", "RRI", "RRIslots", "PDB", "txPerTB",
                "grantIndex", "trigger", "nextFrame", "nextSubframe", "rbStart", "rbLen",
                "reEvalEnabled", "reEvalFrame", "reEvalSubframe"]
RX_COLUMNS = ["rxTime", "packetID", "distance", "txID", "rxID", "decoded", "lossType", "txIndex",
              "trigger", "announced", "latency"]
TX_COLUMNS = ["node", "packet", "genTime", "txIndex", "txTime", "frame", "subframe", "Cresel", "RRI",
              "psschRbStart", "psschRbLen", "psschRbLenTb", "pscchRbStart", "pscchRbLen"]
RESERVATION_COLUMNS = ["node", "time", "unusedSubchannelRatio", "unusedReservations", "reservations",
                       "latencyReselections", "sizeReselections", "counterReselections", "transmissions"]
POS_COLUMNS = ["time", "node", "x", "y", "z", "vx", "vy", "vz", "traffic", "inside"]


def run_highway(*arguments, cwd=None):
    """Runs HIGHWAY in a scratch directory and returns (output folder, stdout)."""
    workdir = cwd or tempfile.mkdtemp(prefix="morev2x-test-")
    env = dict(os.environ, DYLD_LIBRARY_PATH=BUILD)
    done = subprocess.run([HIGHWAY] + list(arguments), cwd=workdir, env=env, timeout=TIMEOUT,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if done.returncode != 0:
        raise AssertionError("HIGHWAY %s failed with %d:\n%s"
                             % (" ".join(arguments), done.returncode, done.stdout[-3000:]))
    results = os.path.join(workdir, "results")
    folders = sorted(os.listdir(results))
    return os.path.join(results, folders[-1]), done.stdout


def read(folder, name, columns):
    return pd.read_csv(os.path.join(folder, name), header=None, names=columns)


class ScratchRuns(unittest.TestCase):
    def tearDown(self):
        for path in getattr(self, "scratch", []):
            shutil.rmtree(path, ignore_errors=True)

    def highway(self, *arguments):
        workdir = tempfile.mkdtemp(prefix="morev2x-test-")
        self.scratch = getattr(self, "scratch", []) + [workdir]
        return run_highway(*arguments, cwd=workdir)


class BaselineHighway(ScratchRuns):
    def test_v2x_tag_reaches_the_mac(self):
        # Without the weak UdpClient::Send the tag is missing and PDB is uninitialised memory
        folder, _ = self.highway("--Vehicles=20", "--simTime=3", "--Periodic", "--mcs=13")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        self.assertGreater(len(ssps), 0)
        self.assertTrue(ssps.PDB.isin([20, 100]).all(), ssps.PDB.unique())
        self.assertTrue(ssps.RRI.isin([20, 100]).all(), ssps.RRI.unique())

    def test_candidate_lists_shrink_in_order(self):
        folder, _ = self.highway("--Vehicles=40", "--simTime=3", "--Periodic", "--mcs=13")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        self.assertTrue((ssps.nCSRfinal <= ssps.nCSRpastTx).all())
        self.assertTrue((ssps.nCSRpastTx <= ssps.nCSRinitial).all())
        self.assertTrue((ssps.nCSRfinal >= 0.2 * ssps.nCSRpastTx).all(), "the 20 % rule of step 1")

    def test_reception_log_is_consistent(self):
        folder, _ = self.highway("--Vehicles=40", "--simTime=3", "--Periodic", "--mcs=13")
        rx = read(folder, "ReceivedLog.txt", RX_COLUMNS)
        self.assertTrue((rx[rx.decoded == 1].lossType == 9).all())
        self.assertTrue(rx[rx.decoded == 0].lossType.isin([0, 1, 2, 3]).all())
        close = rx[rx.distance < 100]
        self.assertGreater(close.decoded.mean(), 0.7, "PDR below 100 m")
        far = rx[rx.distance > 2000]
        self.assertLess(far.decoded.mean(), close.decoded.mean() / 2, "PDR must fall with distance")

    def test_same_seed_is_reproducible(self):
        first, _ = self.highway("--Vehicles=20", "--simTime=2", "--Periodic", "--mcs=13", "--seed=5")
        second, _ = self.highway("--Vehicles=20", "--simTime=2", "--Periodic", "--mcs=13", "--seed=5")
        third, _ = self.highway("--Vehicles=20", "--simTime=2", "--Periodic", "--mcs=13", "--seed=6")
        def ssps(folder):
            with open(os.path.join(folder, "SSPSlog.txt")) as log:
                return log.read()
        self.assertEqual(ssps(first), ssps(second))
        self.assertNotEqual(ssps(first), ssps(third))


class TrafficAndSchedulerModes(ScratchRuns):
    """Every mode the scenario exposes has to reach the end of a short run."""
    MODES = {
        "periodic": ["--Periodic"],
        "aperiodic": ["--Aperiodic"],
        "mixed": ["--Mixed", "--Percentage=50"],
        "adaptive": ["--Mixed", "--Percentage=50", "--Adaptive"],
        "dynamic": ["--Periodic", "--Dynamic"],
        "retx": ["--Periodic", "--ReTx"],
        "reevaluation": ["--Periodic", "--ReEvaluation"],
        "allslots": ["--Periodic", "--ReEvaluation", "--AllSlots"],
        "umh": ["--Periodic", "--ReTx", "--ReEvaluation", "--UMH"],
        "variablesize": ["--Aperiodic", "--VariableSize"],
        "numerology1": ["--Periodic", "--Numerology=1", "--SubChannel=10"],
        "numerology2": ["--Periodic", "--Numerology=2", "--SubChannel=10", "--ChannelBW=20"],
        "freqreuse": ["--Periodic", "--Dynamic", "--FreqReuse", "--ReuseDist=500"],
        "avgrri": ["--Aperiodic", "--AvgRRI"],
        "aperiodic_reeval": ["--Aperiodic", "--ReTx", "--ReEvaluation"],
    }

    def test_all_modes(self):
        for name, flags in self.MODES.items():
            with self.subTest(mode=name):
                folder, _ = self.highway("--Vehicles=15", "--simTime=3", "--mcs=13", *flags)
                self.assertGreater(os.path.getsize(os.path.join(folder, "SSPSlog.txt")), 0)
                self.assertGreater(os.path.getsize(os.path.join(folder, "ReceivedLog.txt")), 0)


class PerNodeSchedulers(ScratchRuns):
    def test_even_ues_keep_sps_by_default(self):
        folder, _ = self.highway("--Vehicles=20", "--simTime=4", "--Periodic", "--mcs=13")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        even = ssps[(ssps.node % 2 == 0) & (ssps.RRI == 100)]
        self.assertTrue((even.Cresel >= 5).all(), even.Cresel.unique())

    def test_legacy_flag_restores_the_upstream_behaviour(self):
        folder, _ = self.highway("--Vehicles=20", "--simTime=4", "--Periodic", "--mcs=13",
                                 "--LegacyEvenUeOneShot=true")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        self.assertTrue((ssps[ssps.node % 2 == 0].Cresel == 1).all())

    def test_sps_and_dynamic_coexist(self):
        folder, _ = self.highway("--Vehicles=20", "--simTime=5", "--Periodic", "--mcs=13",
                                 "--SchedulerMap=1-10:sps;11-20:dynamic")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        sps = ssps[ssps.node <= 10]
        dynamic = ssps[ssps.node > 10]
        self.assertTrue((sps.Cresel >= 5).all(), "SPS UEs must keep multi-period grants")
        self.assertTrue((dynamic.Cresel == 1).all(), "dynamic UEs must reselect for every packet")
        self.assertGreater(len(dynamic) / dynamic.node.nunique(), 3 * len(sps) / sps.node.nunique())

    def test_rsrp_mcs_and_retx_are_per_node(self):
        folder, _ = self.highway("--Vehicles=30", "--simTime=4", "--Periodic", "--mcs=13",
                                 "--SchedulerMap=1-10:rsrp=-126;11-20:mcs=5;21-30:retx")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        tx = read(folder, "phyDebugShort.txt", TX_COLUMNS)
        low = ssps[ssps.node <= 10].rsrpThreshold.min()
        rest = ssps[ssps.node > 10].rsrpThreshold.min()
        self.assertEqual(low, -126)
        self.assertEqual(rest, -128)
        self.assertGreater(tx[(tx.node > 10) & (tx.node <= 20)].psschRbLen.mean(),
                           tx[tx.node <= 10].psschRbLen.mean(), "a lower MCS needs more RBs")
        self.assertTrue((ssps[ssps.node > 20].txPerTB == 2).all())
        self.assertTrue((ssps[ssps.node <= 20].txPerTB == 1).all())

    def test_map_file_records_what_was_applied(self):
        folder, _ = self.highway("--Vehicles=4", "--simTime=1", "--Periodic", "--mcs=13",
                                 "--SchedulerMap=2-3:dynamic+retx+rsrp=-110")
        applied = pd.read_csv(os.path.join(folder, "SchedulerMap.txt"), header=None,
                              names=["node", "dynamic", "retx", "reeval", "rsrp", "mcs", "pkeep"]).set_index("node")
        self.assertEqual(applied.dynamic.tolist(), [0, 1, 1, 0])
        self.assertEqual(applied.retx.tolist(), [0, 1, 1, 0])
        self.assertEqual(applied.rsrp.tolist(), [-128, -110, -110, -128])

    def counter_reselections(self, *flags):
        # SSPSlog.txt is only flushed by a later selection, so it can stay empty here
        folder, _ = self.highway("--Vehicles=12", "--simTime=8", "--Periodic", "--mcs=13", *flags)
        log = read(folder, "ReservationsLog.txt", RESERVATION_COLUMNS)
        return log.groupby("node").counterReselections.sum()

    def test_pkeep_holds_grants_past_the_counter(self):
        selections = self.counter_reselections("--pkeep=1")
        self.assertEqual(selections.max(), 1, "one selection, then the grant is kept")
        self.assertGreater(self.counter_reselections("--pkeep=0").mean(), 1)

    def test_pkeep_is_per_node(self):
        selections = self.counter_reselections("--SchedulerMap=1-6:pkeep=1;7-12:pkeep=0")
        self.assertEqual(selections[selections.index <= 6].max(), 1)
        self.assertGreater(selections[selections.index > 6].mean(), 1)

    def test_unknown_option_is_refused(self):
        with self.assertRaises(AssertionError):
            self.highway("--Vehicles=4", "--simTime=1", "--Periodic", "--mcs=13",
                         "--SchedulerMap=1-2:nonsense")


class SumoMobility(ScratchRuns):
    def sumo_run(self, *extra):
        return self.highway("--simTime=8", "--Periodic", "--mcs=13", "--PosLog=true",
                            "--sumoCfg=" + SUMO_CFG, *extra)

    def test_live_vehicles_follow_the_road(self):
        folder, out = self.sumo_run("--Vehicles=80", "--sumoWarmup=60")
        self.assertIn("vehicles without a node 0", out)
        positions = read(folder, "posFile.txt", POS_COLUMNS)
        active = positions[positions.y > -5e4]
        self.assertGreater(active.node.nunique(), 30)
        # a departed vehicle coasts for SumoMobilityConfig::parkDelay past the end of the road
        self.assertTrue(active.x.between(-10, 2010).all())
        self.assertTrue(active.y.abs().lt(20).all())
        moved = active.sort_values(["node", "time"]).groupby("node").x
        step = (moved.diff().abs() / 0.1).dropna()
        self.assertTrue(step.between(5, 45).mean() > 0.95, "vehicles should move at road speed")

    def test_parked_nodes_stay_silent(self):
        folder, _ = self.sumo_run("--Vehicles=80", "--sumoWarmup=60")
        positions = read(folder, "posFile.txt", POS_COLUMNS)
        tx = read(folder, "phyDebugShort.txt", TX_COLUMNS)
        always_parked = set(positions.node) - set(positions[positions.y > -5e4].node)
        self.assertGreater(len(always_parked), 0, "the pool should be larger than the traffic")
        self.assertFalse(always_parked & set(tx.node))

    def test_logs_survive_sumo_coordinates(self):
        folder, _ = self.sumo_run("--Vehicles=80", "--sumoWarmup=60")
        self.assertGreater(os.path.getsize(os.path.join(folder, "CBR_OutputFile.txt")), 0)
        rx = read(folder, "ReceivedLog.txt", RX_COLUMNS)
        self.assertGreater(rx[rx.distance < 100].decoded.mean(), 0.5)
        self.assertLess(rx.distance.max(), 5000, "a parked node reached the reception log")

    def test_small_pool_rejects_vehicles_without_crashing(self):
        _, out = self.sumo_run("--Vehicles=5", "--sumoWarmup=60")
        self.assertRegex(out, r"Peak active vehicles 5, vehicles without a node [1-9]")

    def test_nodes_are_recycled_when_vehicles_leave(self):
        _, out = self.highway("--simTime=40", "--Periodic", "--mcs=13", "--Vehicles=75",
                              "--sumoCfg=" + SUMO_CFG, "--sumoWarmup=60")
        self.assertIn("vehicles without a node 0", out)

    def test_no_sumo_process_survives(self):
        self.sumo_run("--Vehicles=20", "--sumoWarmup=10")
        running = subprocess.run(["pgrep", "-f", "highway.sumocfg"], stdout=subprocess.PIPE, text=True)
        self.assertEqual(running.stdout.strip(), "")

    def test_trace_replay_matches_the_recording(self):
        workdir = tempfile.mkdtemp(prefix="morev2x-trace-")
        self.scratch = getattr(self, "scratch", []) + [workdir]
        fcd = os.path.join(workdir, "fcd.xml")
        trace = os.path.join(workdir, "trace.csv")
        subprocess.check_call([os.path.join(os.environ["SUMO_HOME"], "bin", "sumo"), "-c", SUMO_CFG,
                               "--end", "75", "--fcd-output", fcd], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
        subprocess.check_call([os.path.join(ROOT, ".venv", "bin", "python"),
                               os.path.join(ROOT, "tools", "fcd2trace.py"), fcd, trace,
                               "--begin", "60", "--end", "75"], stdout=subprocess.DEVNULL)
        recorded = pd.read_csv(trace)
        folder, out = self.highway("--simTime=8", "--Periodic", "--mcs=13", "--PosLog=true",
                                   "--Vehicles=%d" % (recorded.node_id.max() + 1),
                                   "--mobilityTrace=" + trace)
        self.assertIn("vehicles without a node 0", out)
        positions = read(folder, "posFile.txt", POS_COLUMNS)
        sample = positions[(positions.time == 5.0) & (positions.y > -5e4)]
        expected = recorded[recorded.time == 5.0]
        self.assertEqual(len(sample), len(expected))
        merged = sample.assign(node_id=sample.node - 1).merge(expected, on="node_id")
        self.assertLess((merged.x_x - merged.x_y).abs().max(), 0.5)
        tx = read(folder, "phyDebugShort.txt", TX_COLUMNS)
        self.assertGreater(len(tx), 0)


class ParameterSweep(ScratchRuns):
    def test_sweep_runs_every_combination_and_resumes(self):
        workdir = tempfile.mkdtemp(prefix="morev2x-sweep-")
        self.scratch = [workdir]
        config = os.path.join(workdir, "sweep.yaml")
        with open(config, "w") as out:
            out.write("simTime: 3\nmcs: 13\nPeriodic: true\nseed: 1\nVehicles: [4, 6]\nRSRP: [-128, -118]\n")
        output = os.path.join(workdir, "out")
        command = [os.path.join(ROOT, ".venv", "bin", "python"), os.path.join(ROOT, "tools", "run_sweep.py"),
                   config, "--jobs", "2", "--output", output]
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        summary = pd.read_csv(os.path.join(output, "summary.csv"))
        self.assertEqual(sorted(zip(summary.Vehicles, summary.RSRP)),
                         sorted([(4, -128), (4, -118), (6, -128), (6, -118)]))
        self.assertFalse(summary.failed.any())
        self.assertGreater(summary.transmissions.min(), 0)

        marker = os.path.join(output, "Vehicles4_RSRP-128", "stdout.log")
        stamp = os.path.getmtime(marker)
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        self.assertEqual(os.path.getmtime(marker), stamp, "a finished run must not start again")


if __name__ == "__main__":
    unittest.main()
