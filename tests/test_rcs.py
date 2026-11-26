"""Repeated Contention Scheduling (tools/rcs, NrV2XRcsContention) on real HIGHWAY runs."""
import importlib.util
import os
import tempfile
import unittest

import numpy as np

from test_integration import ROOT, RX_COLUMNS, TX_COLUMNS, ScratchRuns, read

# Loaded under its own name: other tests put a different metrics.py on the path
_spec = importlib.util.spec_from_file_location("rcs_metrics", os.path.join(ROOT, "tools", "rcs", "metrics.py"))
metrics = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(metrics)

RCS_COLUMNS = ["time", "node", "packet", "genMs", "rcsFrame", "contention", "outcome", "sc", "delayMs",
               "group", "priority", "neededScs"]
TX_FILE = ["packet", "genTime", "node", "x", "y", "trafficType", "messageType", "size"]
PAPER = ["--Ideal=1", "--Rcs=1", "--ChannelBW=20", "--SubChannel=25", "--PDB=60", "--Periodic=1",
         "--UniformPeriod=100", "--mcs=13", "--UdpFiles=1", "--RxFile=0", "--RxLog=0", "--simTime=3"]


class RcsIdeal(ScratchRuns):
    def check_frames(self, numerology, slot_ms):
        folder, _ = self.highway(*PAPER, "--Numerology=%d" % numerology, "--RcsGroups=36", "--Vehicles=72")
        log = read(folder, "RcsLog.txt", RCS_COLUMNS)
        tx = read(folder, "phyDebugShort.txt", TX_COLUMNS)
        won = log[log.outcome == 0]
        self.assertGreater(len(won), 0)
        self.assertTrue((won.delayMs <= 60).all(), "a won SC is used within the PDB")
        self.assertTrue(log.outcome.isin([0, 1, 2, 3]).all())
        # The PHY sends each won packet once, in the data part of its frame
        sent = tx.merge(won, on="packet")
        self.assertEqual(len(sent), len(tx), "every transmission comes from a won contention")
        self.assertEqual(sent.packet.nunique(), len(sent), "one shot per packet")
        slot = np.rint(sent.txTime * 1000 / slot_ms).astype(int)
        frame_slots = int(round(10 / slot_ms))
        contention_slots = int(round(1 / slot_ms))
        self.assertTrue((slot // frame_slots == sent.rcsFrame).all(), "sent in the frame it won")
        self.assertTrue((slot % frame_slots >= contention_slots).all(), "never in the contention time")
        sc_per_slot = 100 // 25 if numerology == 0 else 50 // 25
        expected = sent.rcsFrame * frame_slots + contention_slots + sent.sc // sc_per_slot
        self.assertTrue((slot == expected).all(), "slot of the SC")
        self.assertTrue((sent.psschRbStart == (sent.sc % sc_per_slot) * 25).all(), "sub-channel of the SC")
        self.assertTrue((sent.psschRbLen == sent.neededScs * 25).all(), "allocated RBs match neededScs")

    def test_frames_mu0(self):
        self.check_frames(0, 1.0)

    def test_frames_mu1(self):
        self.check_frames(1, 0.5)

    def test_every_packet_ends_once(self):
        folder, _ = self.highway(*PAPER, "--Numerology=1", "--RcsGroups=36", "--Vehicles=396", "--simTime=1")
        log = read(folder, "RcsLog.txt", RCS_COLUMNS)
        final = log[~log.outcome.isin([1, 4])]
        self.assertEqual(final.packet.nunique(), len(final), "one final outcome per packet")
        gen = read(folder, "TxFile.txt", TX_FILE)
        early = gen[gen.genTime < log.time.max() - 0.07]
        self.assertTrue(early.packet.isin(final.packet).all(), "no packet is left waiting")
        self.assertGreater((final.outcome == 2).sum(), 0, "a = 1.1 with J = 36 drops packets")


class RcsQoSAndFixes(ScratchRuns):
    def test_incompatible_flags_are_refused(self):
        bad = [
            ["--Rcs=1", "--Dynamic=1"],
            ["--Rcs=1", "--ReTx=1"],
            ["--Rcs=1", "--ReEvaluation=1"],
            ["--Rcs=1", "--AllSlots=1"],
            ["--Rcs=1", "--pkeep=0.5"],
            ["--Rcs=1", "--RcsFrame=10", "--RcsContention=10"],
            ["--Rcs=1", "--SchedulerMap=1-4:dynamic"],
            ["--ns3::NrV2XUeMac::Rcs=true"],
        ]
        for flags in bad:
            with self.subTest(flags=flags):
                with self.assertRaises(AssertionError):
                    self.highway("--Vehicles=4", "--simTime=1", "--Periodic=1", "--mcs=13", *flags)

    def test_allslots_attribute_does_not_reevaluate_rcs(self):
        folder, _ = self.highway("--Vehicles=10", "--simTime=2", "--Periodic=1", "--mcs=13",
                                 "--Rcs=1", "--RcsGroups=9",
                                 "--ns3::NrV2XUeMac::AllSlotsReEvaluation=true")
        reeval = os.path.join(folder, "ReEvaluationsLog.txt")
        self.assertTrue(not os.path.exists(reeval) or os.path.getsize(reeval) == 0)

    def test_extraflow_multisc_and_priority_aware_rcs(self):
        # Primary flow: 165 B + headers -> 1 SC (12 RBs), priority 5
        # ExtraFlow:   300 B + headers -> 2 SCs (24 RBs), priority 2
        folder, _ = self.highway("--Ideal=1", "--Rcs=1", "--RcsPriorityAware=1", "--RcsGroups=1",
                                 "--Numerology=2", "--ChannelBW=20", "--SubChannel=12", "--mcs=14",
                                 "--Vehicles=20", "--simTime=3", "--PDB=60", "--Periodic=1",
                                 "--UniformPeriod=100", "--UdpFiles=1", "--RxFile=0", "--RxLog=0",
                                 "--SchedulerMap=1-20:prio=5", "--ExtraFlow=20:20:300:2:sps")
        log = read(folder, "RcsLog.txt", RCS_COLUMNS)
        tx = read(folder, "phyDebugShort.txt", TX_COLUMNS)
        self.assertEqual(set(log.priority), {2, 5})
        self.assertEqual(set(log.neededScs), {1, 2})
        self.assertEqual(set(tx.psschRbLen), {12, 24})
        self.assertTrue((tx.psschRbStart + tx.psschRbLen <= 24).all())
        self.assertEqual(tx.groupby(["node", "txTime"]).size().max(), 1, "one PSSCH per slot")

    def test_highway_rx_time_and_cbr_accounting(self):
        folder, _ = self.highway("--Vehicles=15", "--simTime=3", "--Periodic=1", "--mcs=13",
                                 "--Rcs=1", "--RcsGroups=9")
        rx = read(folder, "ReceivedLog.txt", RX_COLUMNS)
        self.assertGreater(len(rx), 0)
        frac = np.abs((rx.rxTime * 100.0) - np.rint(rx.rxTime * 100.0))
        self.assertTrue((frac > 1e-3).any(), "RxTimeResolution must preserve sub-10 ms timestamps")
        cbr = read(folder, "CBR_OutputFile.txt", ["node", "cbr", "time"])
        self.assertGreater(len(cbr), 0)
        self.assertTrue(cbr.cbr.between(0.0, 1.0).all())


class RcsMetrics(unittest.TestCase):
    def test_timeliness_of_a_regular_flow(self):
        times = np.arange(0, 2, 0.1)
        out = metrics.timeliness(times, np.zeros(len(times)))
        self.assertAlmostEqual(out["pir_ms"], 100)
        self.assertAlmostEqual(out["aoi_ms"], 50)
        self.assertAlmostEqual(out["aoi_violation_100"], 0)

    def test_timeliness_with_a_gap(self):
        # receptions at 0, 100 and 400 ms: Y = 100, 300
        out = metrics.timeliness(np.array([0, 0.1, 0.4]), np.zeros(3))
        self.assertAlmostEqual(out["pir_ms"], 200)
        self.assertAlmostEqual(out["aoi_ms"], (100 ** 2 + 300 ** 2) / (2 * 400))
        self.assertAlmostEqual(out["aoi_violation_100"], 200 / 400)
        self.assertAlmostEqual(out["aoi_violation_200"], 100 / 400)

    def test_timeliness_with_delays_and_full_aoi(self):
        # Receptions at 0.03, 0.13, 0.23 with constant delay 0.03 s -> Y = 100 ms, d_prev = 30 ms
        times = np.array([0.03, 0.13, 0.23])
        delays = np.array([0.03, 0.03, 0.03])
        out = metrics.timeliness(times, np.zeros(3), delays=delays)
        self.assertAlmostEqual(out["aoi_ms"], 50.0)
        self.assertAlmostEqual(out["aoi_violation_100"], 0.0)
        self.assertAlmostEqual(out["aoi_full_ms"], 80.0)
        # Age rises from 30 ms to 130 ms on each 100 ms interval -> 30 ms > 100 ms per interval
        self.assertAlmostEqual(out["aoi_full_violation_100"], 0.3)

    def test_ideal_detects_window_boundary_collisions(self):
        with tempfile.TemporaryDirectory(prefix="rcs-metrics-") as folder:
            # Packet 1 generated before start=1.0 s, transmits in the same (slot, sc) as packet 2 inside window
            with open(os.path.join(folder, "TxFile.txt"), "w") as f:
                f.write("1,0.995,1,0,0,0,0,200\n")
                f.write("2,1.000,2,0,0,0,0,200\n")
                f.write("3,1.020,2,0,0,0,0,200\n")
            with open(os.path.join(folder, "phyDebugShort.txt"), "w") as f:
                f.write("1,1,0.995,1,1.002,2,1,0,0,0,25,25,0,25\n")
                f.write("2,2,1.000,1,1.002,2,1,0,0,0,25,25,0,25\n")
                f.write("2,3,1.020,1,1.022,2,3,0,0,25,25,25,25,25\n")
                f.write("2,4,1.200,1,1.202,2,4,0,0,0,25,25,0,25\n")
            out = metrics.ideal(folder, slot_ms=1.0, pdb_ms=60.0, start=1.0, subchannel_rbs=25)
            self.assertEqual(out["packets"], 2)
            self.assertAlmostEqual(out["pcoll"], 0.5)
            self.assertAlmostEqual(out["prr"], 0.5)


if __name__ == "__main__":
    unittest.main()
