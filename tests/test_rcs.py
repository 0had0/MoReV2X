"""Repeated Contention Scheduling (tools/rcs, NrV2XRcsContention) on real HIGHWAY runs."""
import os
import sys
import unittest

import numpy as np
import pandas as pd

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "rcs"))
import metrics  # noqa: E402
from test_integration import TX_COLUMNS, ScratchRuns, read  # noqa: E402

RCS_COLUMNS = ["time", "node", "packet", "genMs", "rcsFrame", "contention", "outcome", "sc", "delayMs"]
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

    def test_frames_mu0(self):
        self.check_frames(0, 1.0)

    def test_frames_mu1(self):
        self.check_frames(1, 0.5)

    def test_every_packet_ends_once(self):
        folder, _ = self.highway(*PAPER, "--Numerology=1", "--RcsGroups=36", "--Vehicles=396", "--simTime=1")
        log = read(folder, "RcsLog.txt", RCS_COLUMNS)
        final = log[log.outcome != 1]
        self.assertEqual(final.packet.nunique(), len(final), "one final outcome per packet")
        gen = read(folder, "TxFile.txt", TX_FILE)
        early = gen[gen.genTime < log.time.max() - 0.07]
        self.assertTrue(early.packet.isin(final.packet).all(), "no packet is left waiting")
        self.assertGreater((final.outcome == 2).sum(), 0, "a = 1.1 with J = 36 drops packets")


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


if __name__ == "__main__":
    unittest.main()
