"""HIGHWAY --Traffic: the traffic models of groupcast-adaptation-ns3 for every vehicle."""
import os
import subprocess
import tempfile
import unittest

import numpy as np

from test_integration import BUILD, HIGHWAY, SSPS_COLUMNS, ScratchRuns, read

TX_FILE = ["packet", "genTime", "node", "x", "y", "messageType", "trafficType", "size"]
BASE = ["--Vehicles=20", "--simTime=4", "--mcs=13", "--UdpFiles=1"]


def gaps_ms(tx):
    """Intervals between consecutive packets of each vehicle, in ms."""
    tx = tx.sort_values(["node", "genTime"])
    return (tx.groupby("node").genTime.diff().dropna() * 1000.0).to_numpy()


def whole(values, unit):
    """Every value is a whole number of units, up to the 6 digits of the log."""
    ratio = values / unit
    return bool((np.abs(ratio - np.round(ratio)) * unit < 0.01).all())


class TrafficModels(ScratchRuns):
    def run_model(self, model):
        folder, out = self.highway(*BASE, "--Traffic=" + model)
        tx = read(folder, "TxFile.txt", TX_FILE)
        self.assertGreater(len(tx), 20, model)
        tx["payload"] = tx["size"] - 34
        self.assertIn("traffic model = " + model.split(":")[0], open(os.path.join(folder, "simREADME.txt")).read())
        return folder, tx

    def test_periodic1_sizes_in_turn(self):
        folder, tx = self.run_model("periodic1")
        self.assertEqual(set(tx.payload), {300, 190})
        self.assertTrue(whole(gaps_ms(tx), 100))
        for _, sizes in tx.sort_values("genTime").groupby("node").payload:
            big = np.flatnonzero(sizes.to_numpy() == 300)
            self.assertTrue((np.diff(big) == 5).all(), "300 B every fifth packet")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        self.assertTrue(ssps.RRI.isin([100]).all(), set(ssps.RRI))

    def test_periodic2_and_5(self):
        _, tx = self.run_model("periodic2")
        self.assertEqual(set(tx.payload), {800, 1200})
        self.assertTrue(whole(gaps_ms(tx), 10))
        self.assertAlmostEqual((tx.payload == 1200).mean(), 0.2, delta=0.05)
        _, tx = self.run_model("periodic5")
        self.assertEqual(set(tx.payload), {1200})
        self.assertTrue(whole(gaps_ms(tx), 50))

    def test_aperiodic1_capped_and_dynamic(self):
        folder, tx = self.run_model("aperiodic1:600")
        self.assertTrue(set(tx.payload) <= {200, 400, 600})
        gaps = gaps_ms(tx)
        self.assertGreaterEqual(gaps.min(), 50 - 0.01)
        self.assertTrue(whole(gaps, 1), "50 ms + whole ms")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        self.assertTrue((ssps.Cresel <= 1).all(), "one selection per packet")

    def test_scheduler_map_priority(self):
        folder, _ = self.highway(*BASE, "--Traffic=periodic1", "--SchedulerMap=1-10:prio=2;11-20:prio=6")
        ssps = read(folder, "SSPSlog.txt", SSPS_COLUMNS)
        self.assertEqual(set(ssps[ssps.node <= 10].priority), {2})
        self.assertEqual(set(ssps[ssps.node > 10].priority), {6})

    def test_refused_models(self):
        env = dict(os.environ, DYLD_LIBRARY_PATH=BUILD, LD_LIBRARY_PATH=BUILD)
        workdir = tempfile.mkdtemp(prefix="morev2x-test-")
        self.scratch = getattr(self, "scratch", []) + [workdir]
        for model in ("periodic3", "periodic7"):
            done = subprocess.run([HIGHWAY] + BASE + ["--Traffic=" + model], cwd=workdir, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
            self.assertNotEqual(done.returncode, 0, model)


if __name__ == "__main__":
    unittest.main()
