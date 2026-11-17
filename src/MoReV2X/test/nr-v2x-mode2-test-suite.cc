/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <list>
#include <map>
#include <set>
#include <vector>

#include "ns3/boolean.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/enum.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/double.h"
#include "ns3/nist-sl-pool.h"
#include "ns3/string.h"
#include "ns3/nr-v2x-amc.h"
#include "ns3/nr-v2x-adaptive-reservation.h"
#include "ns3/nr-v2x-rcs-contention.h"
#include "ns3/nr-v2x-ue-mac.h"
#include "ns3/nr-v2x-utils.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

using namespace ns3;

class SubtractFramesTestCase : public TestCase
{
public:
  SubtractFramesTestCase () : TestCase ("SubtractFrames counts slots across the SFN wrap") {}

private:
  virtual void DoRun (void)
  {
    NS_TEST_ASSERT_MSG_EQ (SubtractFrames (10, 10, 5, 2), 3, "same frame");
    NS_TEST_ASSERT_MSG_EQ (SubtractFrames (11, 10, 1, 9), 2, "next frame");
    NS_TEST_ASSERT_MSG_EQ (SubtractFrames (2, 1023, 0, 0), 30, "SFN wrap");
  }
};

class SubframeSubtractionTestCase : public TestCase
{
public:
  SubframeSubtractionTestCase () : TestCase ("SubframeInfo subtraction counts slots across the SFN wrap") {}

private:
  virtual void DoRun (void)
  {
    typedef SidelinkCommResourcePool::SubframeInfo Slot;
    Slot later, earlier;

    later.frameNo = 10; later.subframeNo = 5;
    earlier.frameNo = 9; earlier.subframeNo = 8;
    Slot gap = later - earlier;
    NS_TEST_ASSERT_MSG_EQ (gap.frameNo * 10 + gap.subframeNo, 7, "same SFN cycle");

    later.frameNo = 0; later.subframeNo = 2;
    earlier.frameNo = 1023; earlier.subframeNo = 9;
    gap = later - earlier;
    NS_TEST_ASSERT_MSG_EQ (gap.frameNo * 10 + gap.subframeNo, 3, "across the SFN wrap");
  }
};

class SelectionTimersTestCase : public TestCase
{
public:
  SelectionTimersTestCase () : TestCase ("Processing times follow the numerology") {}

private:
  virtual void DoRun (void)
  {
    NS_TEST_ASSERT_MSG_EQ (GetTproc0 (0), 1, "T_proc,0 at 15 kHz");
    NS_TEST_ASSERT_MSG_EQ (GetTproc0 (2), 2, "T_proc,0 at 60 kHz");
    NS_TEST_ASSERT_MSG_EQ (GetTproc1 (0), 3, "T_proc,1 at 15 kHz");
    NS_TEST_ASSERT_MSG_EQ (GetTproc1 (1), 5, "T_proc,1 at 30 kHz");
    NS_TEST_ASSERT_MSG_EQ (GetRbsFromBW (15, 10), 52, "10 MHz at 15 kHz");
  }
};

class ReselectionCounterTestCase : public TestCase
{
public:
  ReselectionCounterTestCase () : TestCase ("Cresel draws stay inside the 3GPP interval") {}

private:
  virtual void DoRun (void)
  {
    for (uint32_t i = 0; i < 500; ++i)
      {
        const uint32_t at100ms = GetCresel (100);
        NS_TEST_ASSERT_MSG_GT_OR_EQ (at100ms, 5, "RRI 100 ms lower bound");
        NS_TEST_ASSERT_MSG_LT_OR_EQ (at100ms, 15, "RRI 100 ms upper bound");

        const uint32_t at20ms = GetCresel (20);
        NS_TEST_ASSERT_MSG_GT_OR_EQ (at20ms, 25, "RRI 20 ms lower bound");
        NS_TEST_ASSERT_MSG_LT_OR_EQ (at20ms, 75, "RRI 20 ms upper bound");
      }
  }
};

class ResidualCsrTestCase : public TestCase
{
public:
  ResidualCsrTestCase () : TestCase ("Residual CSRs count every candidate of every subchannel") {}

private:
  virtual void DoRun (void)
  {
    typedef std::list<SidelinkCommResourcePool::SubframeInfo> Slots;
    std::map<uint16_t, Slots> candidates;
    NS_TEST_ASSERT_MSG_EQ (ComputeResidualCSRs (candidates), 0, "empty map");

    SidelinkCommResourcePool::SubframeInfo slot;
    slot.frameNo = 1;
    slot.subframeNo = 2;
    candidates[0] = Slots (3, slot);
    candidates[4] = Slots (2, slot);
    NS_TEST_ASSERT_MSG_EQ (ComputeResidualCSRs (candidates), 5, "3 + 2 candidates");
  }
};

/**
 * Scheduler settings are per MAC instance. Two instances configured differently
 * must not affect each other nor the defaults a third one is created with.
 */
class PerInstanceSchedulerTestCase : public TestCase
{
public:
  PerInstanceSchedulerTestCase () : TestCase ("Scheduler attributes are independent per MAC instance") {}

private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XUeMac> sps = CreateObject<NrV2XUeMac> ();
    Ptr<NrV2XUeMac> dynamic = CreateObject<NrV2XUeMac> ();

    dynamic->SetAttribute ("DynamicScheduling", BooleanValue (true));
    dynamic->SetAttribute ("EnableReTx", BooleanValue (true));
    dynamic->SetAttribute ("RSRPthreshold", DoubleValue (-110.0));
    dynamic->SetAttribute ("SlGrantMcs", UintegerValue (7));

    BooleanValue flag;
    DoubleValue threshold;
    UintegerValue mcs;

    sps->GetAttribute ("DynamicScheduling", flag);
    NS_TEST_ASSERT_MSG_EQ (flag.Get (), false, "DynamicScheduling leaked to the other MAC");
    sps->GetAttribute ("EnableReTx", flag);
    NS_TEST_ASSERT_MSG_EQ (flag.Get (), false, "EnableReTx leaked to the other MAC");
    sps->GetAttribute ("RSRPthreshold", threshold);
    NS_TEST_ASSERT_MSG_EQ_TOL (threshold.Get (), -128.0, 1e-9, "RSRPthreshold leaked to the other MAC");

    dynamic->GetAttribute ("DynamicScheduling", flag);
    NS_TEST_ASSERT_MSG_EQ (flag.Get (), true, "DynamicScheduling was not applied");
    dynamic->GetAttribute ("SlGrantMcs", mcs);
    NS_TEST_ASSERT_MSG_EQ (mcs.Get (), 7, "SlGrantMcs was not applied");

    dynamic->SetAttribute ("Pkeep", DoubleValue (0.8));
    DoubleValue keep;
    sps->GetAttribute ("Pkeep", keep);
    NS_TEST_ASSERT_MSG_EQ_TOL (keep.Get (), 0.0, 1e-9, "Pkeep leaked to the other MAC");

    Ptr<NrV2XUeMac> fresh = CreateObject<NrV2XUeMac> ();
    fresh->GetAttribute ("DynamicScheduling", flag);
    NS_TEST_ASSERT_MSG_EQ (flag.Get (), false, "defaults were modified");
  }
};

class AdaptiveReservationTestCase : public TestCase
{
public:
  AdaptiveReservationTestCase () : TestCase ("Adaptive reservation follows the sensed load") {}

private:
  virtual void DoRun (void)
  {
    const std::vector<double> noNeighbours;
    double rri; // the test macros evaluate their argument more than once

    NrV2XAdaptiveReservation congested;
    rri = congested.Update (60, 0.1, noNeighbours);
    NS_TEST_ASSERT_MSG_EQ_TOL (rri, 100, 1e-9, "a busy channel slows the UE");
    NS_TEST_ASSERT_MSG_EQ_TOL (congested.GetKeepProbability (), 0.25, 1e-9, "a busy channel lowers the keep probability");
    rri = congested.Update (100, 0.1, noNeighbours);
    NS_TEST_ASSERT_MSG_EQ_TOL (rri, 100, 1e-9, "RRI capped at the maximum");

    NrV2XAdaptiveReservation free;
    rri = free.Update (60, 0.9, noNeighbours);
    NS_TEST_ASSERT_MSG_EQ_TOL (rri, 20, 1e-9, "a free channel speeds the UE up");
    NS_TEST_ASSERT_MSG_EQ_TOL (free.GetKeepProbability (), 0.75, 1e-9, "a free channel raises the keep probability");
    rri = free.Update (20, 0.9, noNeighbours);
    NS_TEST_ASSERT_MSG_EQ_TOL (rri, 20, 1e-9, "RRI floored at the minimum");
    NS_TEST_ASSERT_MSG_EQ_TOL (free.GetKeepProbability (), 0.8, 1e-9, "keep probability capped");

    NrV2XAdaptiveReservation steady;
    rri = steady.Update (60, 0.4, noNeighbours);
    NS_TEST_ASSERT_MSG_EQ_TOL (rri, 60, 1e-9, "between the thresholds nothing moves");
    NS_TEST_ASSERT_MSG_EQ_TOL (steady.GetKeepProbability (), 0.5, 1e-9, "between the thresholds nothing moves");

    std::vector<double> neighbours;
    neighbours.push_back (100);
    neighbours.push_back (100);
    rri = steady.Update (60, 0.4, neighbours);
    NS_TEST_ASSERT_MSG_EQ_TOL (rri, 90, 1e-9, "pulled towards neighbours, on the 10 ms grid");
  }
};

class LegacyHackDefaultTestCase : public TestCase
{
public:
  LegacyHackDefaultTestCase () : TestCase ("The even-UE one-shot hack stays off by default") {}

private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XUeMac> mac = CreateObject<NrV2XUeMac> ();
    BooleanValue legacy;
    mac->GetAttribute ("LegacyEvenUeOneShot", legacy);
    NS_TEST_ASSERT_MSG_EQ (legacy.Get (), false, "even UEs would silently lose SPS");
  }
};


// GetSlMaxTbSize is the largest PDU that GetSlSubchAndTbSizeFromMcs places inside the channel
class MaxTbSizeTestCase : public TestCase
{
public:
  MaxTbSizeTestCase () : TestCase ("Largest TB of a slot matches the per-PDU TB rule") {}
private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XAmc> amc = CreateObject<NrV2XAmc> ();
    const uint16_t channels[][2] = {{12, 24}, {10, 24}, {10, 52}, {10, 11}};
    for (const auto &c : channels)
      {
        for (int mcs : {5, 13, 14, 20})
          {
            uint32_t maxTb = amc->GetSlMaxTbSize (mcs, c[0], c[1]);
            uint16_t rbs = (c[1] / c[0]) * c[0];
            NS_TEST_ASSERT_MSG_GT (maxTb, 0u, "a whole channel carries something");
            uint16_t sub, rb;
            uint32_t tb = amc->GetSlSubchAndTbSizeFromMcs (maxTb, mcs, c[0], c[1], &sub, &rb) / 8;
            NS_TEST_ASSERT_MSG_GT_OR_EQ (tb, maxTb, "the largest PDU fits its TB");
            NS_TEST_ASSERT_MSG_EQ ((sub <= rbs && rb <= rbs), true, "and stays inside the subchannels");
          }
      }
    // 20 MHz at 60 kHz, 12 RB subchannels, MCS 14: the configuration of the QoS scenarios
    NS_TEST_ASSERT_MSG_GT (amc->GetSlMaxTbSize (14, 12, 24), 550u, "a 550 B CPM fits one slot");
  }
};

// Repeated Contention Scheduling: a fresh contention channel with J groups, K SCs, M sub-carriers
static Ptr<NrV2XRcsContention>
RcsChannel (uint32_t groups, uint32_t scPerFrame, uint32_t subcarriers, NrV2XRcsContention::ToneMode mode)
{
  NrV2XRcsContention::Reset ();
  Ptr<NrV2XRcsContention> channel = NrV2XRcsContention::Get ();
  channel->SetAttribute ("Groups", UintegerValue (groups));
  channel->SetAttribute ("ScPerFrame", UintegerValue (scPerFrame));
  channel->SetAttribute ("Subcarriers", UintegerValue (subcarriers));
  channel->SetAttribute ("ToneMode", EnumValue (mode));
  return channel;
}

static Ptr<MobilityModel>
At (double x)
{
  Ptr<ConstantPositionMobilityModel> mobility = CreateObject<ConstantPositionMobilityModel> ();
  mobility->SetPosition (Vector (x, 0, 0));
  return mobility;
}

// A UE of the tests: like NrV2XUeMac, it draws its group and its tones from its own choice
class RcsTestUe : public Object
{
public:
  RcsTestUe (Ptr<NrV2XRcsChoice> choice, const NrV2XRcsPacket &packet)
    : m_choice (choice), m_packet (packet), m_tones (0) {}
  uint32_t Tone (uint32_t round, uint32_t subcarriers)
  {
    m_tones++;
    return m_choice->ChooseTone (subcarriers, round, m_packet);
  }
  Ptr<NrV2XRcsChoice> m_choice;
  NrV2XRcsPacket m_packet;
  uint32_t m_tones;  ///< tones asked by the contention
};

static Ptr<RcsTestUe>
Join (Ptr<NrV2XRcsContention> channel, uint64_t frame, uint32_t node, Ptr<MobilityModel> mobility,
      const NrV2XRcsPacket &packet = NrV2XRcsPacket (), Ptr<NrV2XRcsChoice> choice = 0)
{
  Ptr<RcsTestUe> ue = CreateObject<RcsTestUe> (choice == 0 ? CreateObject<NrV2XRcsChoice> () : choice, packet);
  channel->Register (frame, node, mobility, ue->m_choice->ChooseGroup (channel->GetGroups (), packet),
                     MakeCallback (&RcsTestUe::Tone, ue));
  return ue;
}

class RcsSingleContenderTestCase : public TestCase
{
public:
  RcsSingleContenderTestCase () : TestCase ("RCS: a lone contender wins the first SC of its group") {}

private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XRcsContention> channel = RcsChannel (1, 36, 400, NrV2XRcsContention::TONE_ALL);
    Join (channel, 7, 1, At (0));
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (7, 1), 0, "nobody to hear, so k = 0");
    NS_TEST_ASSERT_MSG_EQ (channel->GetSubcarriersPerGroup (), 400u, "m = M / J");
    NrV2XRcsContention::Reset ();
  }
};

class RcsFewContendersAllWinTestCase : public TestCase
{
public:
  RcsFewContendersAllWinTestCase () : TestCase ("RCS: with J = 1 and fewer contenders than SCs everybody wins") {}

private:
  virtual void DoRun (void)
  {
    // A node leaves only after hearing w = 36 distinct lower sub-carriers
    Ptr<NrV2XRcsContention> channel = RcsChannel (1, 36, 400, NrV2XRcsContention::TONE_ALL);
    for (uint32_t node = 1; node <= 20; node++)
      {
        Join (channel, 3, node, At (0));
      }
    std::set<int32_t> scs;
    for (uint32_t node = 1; node <= 20; node++)
      {
        int32_t sc = channel->GetResult (3, node);
        NS_TEST_ASSERT_MSG_EQ ((sc >= 0 && sc < 20), true, "k counts the lower picks, at most 19");
        scs.insert (sc);
      }
    NS_TEST_ASSERT_MSG_GT (scs.size (), 15u, "only equal picks share an SC");
    NrV2XRcsContention::Reset ();
  }
};

class RcsOneScPerGroupTestCase : public TestCase
{
public:
  RcsOneScPerGroupTestCase () : TestCase ("RCS: with J = K every group fills its single SC") {}

private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XRcsContention> channel = RcsChannel (36, 36, 400, NrV2XRcsContention::TONE_ALL);
    for (uint32_t node = 1; node <= 360; node++)
      {
        Join (channel, 9, node, At (0));
      }
    std::map<int32_t, uint32_t> winners;
    for (uint32_t node = 1; node <= 360; node++)
      {
        int32_t sc = channel->GetResult (9, node);
        if (sc >= 0)
          {
            winners[sc]++;
          }
      }
    NS_TEST_ASSERT_MSG_EQ (winners.size (), 36u, "ten contenders per group, each group wins its SC");
    uint32_t total = 0;
    for (std::map<int32_t, uint32_t>::iterator it = winners.begin (); it != winners.end (); it++)
      {
        total += it->second;
      }
    NS_TEST_ASSERT_MSG_LT (total, 46u, "three rounds on 11 sub-carriers leave few ties");
    NrV2XRcsContention::Reset ();
  }
};

class RcsDrawTestCase : public TestCase
{
public:
  RcsDrawTestCase () : TestCase ("RCS: the choices follow the weights of their distribution") {}

private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XRcsChoice> choice = CreateObject<NrV2XRcsChoice> ();
    std::vector<double> only1 (3, 0.0);
    only1[1] = 2.5;
    std::vector<double> threeToOne (2, 1.0);
    threeToOne[0] = 3.0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < 20000; i++)
      {
        NS_TEST_ASSERT_MSG_EQ (choice->Draw (only1), 1u, "zero weights are never drawn");
        first += choice->Draw (threeToOne) == 0 ? 1 : 0;
      }
    NS_TEST_ASSERT_MSG_EQ_TOL (first / 20000.0, 0.75, 0.02, "weights 3 and 1");
    NrV2XRcsPacket packet;
    NS_TEST_ASSERT_MSG_EQ (choice->GroupDistribution (6, packet).size (), 6u, "one weight per group");
    NS_TEST_ASSERT_MSG_EQ (choice->ToneDistribution (11, 0, packet).size (), 11u, "one weight per sub-carrier");
  }
};

// A choice skewed by priority: priority 1 always takes the lowest sub-carrier of group 2,
// any other packet the highest sub-carrier of group 2
class RcsPriorityChoice : public NrV2XRcsChoice
{
public:
  virtual std::vector<double> GroupDistribution (uint32_t groups, const NrV2XRcsPacket &packet) const
  {
    std::vector<double> weights (groups, 0.0);
    weights[2] = 1.0;
    return weights;
  }
  virtual std::vector<double> ToneDistribution (uint32_t subcarriers, uint32_t round, const NrV2XRcsPacket &packet) const
  {
    std::vector<double> weights (subcarriers, 0.0);
    weights[packet.priority == 1 ? 0 : subcarriers - 1] = 1.0;
    return weights;
  }
};

class RcsSkewedChoiceTestCase : public TestCase
{
public:
  RcsSkewedChoiceTestCase () : TestCase ("RCS: a skewed distribution decides the group and the SC") {}

private:
  virtual void DoRun (void)
  {
    // J = 6: w = 6 SCs and m = 66 sub-carriers per group, group 2 holds SCs 12 to 17
    Ptr<NrV2XRcsContention> channel = RcsChannel (6, 36, 400, NrV2XRcsContention::TONE_ALL);
    Ptr<NrV2XRcsChoice> choice = CreateObject<RcsPriorityChoice> ();
    NrV2XRcsPacket urgent;
    urgent.priority = 1;
    NrV2XRcsPacket background;
    background.priority = 8;
    Join (channel, 5, 1, At (0), background, choice);
    Join (channel, 5, 2, At (0), urgent, choice);
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (5, 2), 12, "lowest tone, first SC of group 2");
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (5, 1), 13, "heard one lower tone, second SC of group 2");
    NrV2XRcsContention::Reset ();
  }
};

// Always group 0, always the same sub-carrier
class RcsFixedTone : public NrV2XRcsChoice
{
public:
  RcsFixedTone (uint32_t tone) : m_tone (tone) {}
  virtual std::vector<double> GroupDistribution (uint32_t groups, const NrV2XRcsPacket &packet) const
  {
    std::vector<double> weights (groups, 0.0);
    weights[0] = 1.0;
    return weights;
  }
  virtual std::vector<double> ToneDistribution (uint32_t subcarriers, uint32_t round, const NrV2XRcsPacket &packet) const
  {
    std::vector<double> weights (subcarriers, 0.0);
    weights[m_tone] = 1.0;
    return weights;
  }
  uint32_t m_tone;
};

class RcsUeToneTestCase : public TestCase
{
public:
  RcsUeToneTestCase () : TestCase ("RCS: the contention asks each UE for its tone while it is still in") {}

private:
  virtual void DoRun (void)
  {
    // J = 1, w = 36: nobody leaves, both UEs play the three rounds
    Ptr<NrV2XRcsContention> channel = RcsChannel (1, 36, 400, NrV2XRcsContention::TONE_ALL);
    Ptr<RcsTestUe> low = Join (channel, 2, 1, At (0), NrV2XRcsPacket (), CreateObject<RcsFixedTone> (0));
    Ptr<RcsTestUe> high = Join (channel, 2, 2, At (0), NrV2XRcsPacket (), CreateObject<RcsFixedTone> (5));
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (2, 2), 1, "heard tone 0");
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (2, 1), 0, "heard nothing lower");
    NS_TEST_ASSERT_MSG_EQ (low->m_tones, 3u, "three rounds");
    NS_TEST_ASSERT_MSG_EQ (high->m_tones, 3u, "three rounds");

    // J = K, w = 1, both in group 0: the higher tone leaves after the first round
    channel = RcsChannel (36, 36, 400, NrV2XRcsContention::TONE_ALL);
    low = Join (channel, 2, 1, At (0), NrV2XRcsPacket (), CreateObject<RcsFixedTone> (0));
    high = Join (channel, 2, 2, At (0), NrV2XRcsPacket (), CreateObject<RcsFixedTone> (5));
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (2, 1), 0, "wins the SC of group 0");
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (2, 2), -1, "lost");
    NS_TEST_ASSERT_MSG_EQ (low->m_tones, 3u, "the lowest tone stays three rounds");
    NS_TEST_ASSERT_MSG_EQ (high->m_tones, 1u, "asked once, then out");
    NrV2XRcsContention::Reset ();
  }
};

class RcsToneHearingTestCase : public TestCase
{
public:
  RcsToneHearingTestCase () : TestCase ("RCS: tones are heard by all, within a range, or above a power") {}

private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XRcsContention> channel = RcsChannel (1, 36, 400, NrV2XRcsContention::TONE_ALL);
    NS_TEST_ASSERT_MSG_EQ (channel->Hears (At (0), At (1e5)), true, "All");
    channel->SetAttribute ("ToneMode", EnumValue (NrV2XRcsContention::TONE_RANGE));
    channel->SetAttribute ("ToneRange", DoubleValue (300));
    NS_TEST_ASSERT_MSG_EQ (channel->Hears (At (0), At (200)), true, "within the range");
    NS_TEST_ASSERT_MSG_EQ (channel->Hears (At (0), At (400)), false, "beyond the range");
    channel->SetAttribute ("ToneMode", EnumValue (NrV2XRcsContention::TONE_POWER));
    channel->SetAttribute ("ToneThreshold", DoubleValue (-110));
    channel->SetPropagationLossModel (CreateObject<FriisPropagationLossModel> ());
    NS_TEST_ASSERT_MSG_EQ (channel->Hears (At (0), At (200)), true, "about -70 dBm at 200 m");
    NS_TEST_ASSERT_MSG_EQ (channel->Hears (At (0), At (1e5)), false, "about -124 dBm at 100 km");

    // Two contenders out of each other's range both count no lower tone: both take SC 0
    channel->SetAttribute ("ToneMode", EnumValue (NrV2XRcsContention::TONE_RANGE));
    Join (channel, 4, 1, At (0));
    Join (channel, 4, 2, At (1000));
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (4, 1), 0, "hears nobody");
    NS_TEST_ASSERT_MSG_EQ (channel->GetResult (4, 2), 0, "reuses the SC 1 km away");
    NrV2XRcsContention::Reset ();
  }
};

// sl-ThresRSRP-pi-pj and sl-xPercentage, TS 38.214 8.1.4 steps 5 and 6
class PriorityThresholdTestCase : public TestCase
{
public:
  PriorityThresholdTestCase () : TestCase ("Priority-aware RSRP thresholds and X%") {}
private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XUeMac> mac = CreateObject<NrV2XUeMac> ();
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetPriorityThresholdOffset (2, 6), 0.0, 1e-12, "off by default");
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetSizeThreshold (3), 0.2, 1e-12, "X% is 20 % by default");

    mac->SetAttribute ("PriorityAwareSensing", BooleanValue (true));
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetPriorityThresholdOffset (6, 2), 12.0, 1e-12,
                               "a lower priority reservation is 4 x 3 dB easier to reuse");
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetPriorityThresholdOffset (2, 6), -12.0, 1e-12,
                               "a higher priority reservation is excluded 12 dB earlier");
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetPriorityThresholdOffset (0, 6), 0.0, 1e-12, "no priority in the SCI");

    std::ostringstream table;
    for (int i = 0; i < 64; ++i)
      {
        table << (i ? "," : "") << -130 + i;
      }
    mac->SetAttribute ("ThresRsrpList", StringValue (table.str ()));
    // TS 38.214 field i = p_i + (p_j - 1) * 8 = 2 + 4 * 8 = 34 (1-based), entry 33: -97 dBm, base -128
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetPriorityThresholdOffset (2, 5), 31.0, 1e-12, "table entry relative to the base");
    // and the transposed pair p_i = 5, p_j = 2: field 5 + 8 = 13, entry 12, -118 dBm
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetPriorityThresholdOffset (5, 2), 10.0, 1e-12, "the pair is ordered (received, own)");

    mac->SetAttribute ("SizeThresholdList", StringValue ("0.2,0.2,0.35,0.35,0.5,0.5,0.5,0.5"));
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetSizeThreshold (5), 0.5, 1e-12, "X% of priority 5");
    NS_TEST_ASSERT_MSG_EQ_TOL (mac->GetSizeThreshold (0), 0.2, 1e-12, "no priority keeps 20 %");
  }
};

// TS 38.321 5.22.1.1: an announced resource is pre-empted only by a higher priority reservation
class PreemptionTestCase : public TestCase
{
public:
  PreemptionTestCase () : TestCase ("Pre-emption needs a higher priority above the threshold") {}
private:
  bool Check (Ptr<NrV2XUeMac> mac, uint8_t txPriority, uint16_t csr, uint32_t checkFrame, double threshold)
  {
    SidelinkCommResourcePool::SubframeInfo now, check;
    now.frameNo = 12;
    now.subframeNo = 0;
    check.frameNo = checkFrame;
    check.subframeNo = 2;
    uint32_t by = 0;
    return mac->IsPreempted (now, check, csr, 1, 100.0, threshold, txPriority, &by);
  }
  virtual void DoRun (void)
  {
    Ptr<NrV2XUeMac> mac = CreateObject<NrV2XUeMac> ();
    mac->SetAttribute ("NumerologyIndex", UintegerValue (0));
    mac->SetAttribute ("SlotDuration", DoubleValue (1.0));
    mac->SetAttribute ("SubchannelSize", UintegerValue (10));
    // UE 7, priority 2, RSRP -90 dBm, received at SF(10,3) (1-based), RRI 100 ms on subchannel 0.
    // 0-based it was received at (9,2) and reserves (19,2)
    SidelinkCommResourcePool::SubframeInfo received, reserved;
    received.frameNo = 10;
    received.subframeNo = 3;
    reserved.frameNo = 20;
    reserved.subframeNo = 3;
    mac->GetNistLteUePhySapUser ()->ReportPsschRsrpReservation (Seconds (0.1), 0, 10, -90.0, received, reserved,
                                                                5, 7, 100.0, false, false, 2);
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 5, 0, 19, -110.0), true, "priority 2 pre-empts priority 5");
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 2, 0, 19, -110.0), false, "same priority does not pre-empt");
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 1, 0, 19, -110.0), false, "nothing pre-empts priority 1");
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 5, 0, 19, -80.0), false, "below the threshold");
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 5, 1, 19, -110.0), false, "another subchannel");
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 5, 0, 18, -110.0), false, "another slot");
    // Step 6 (c) also counts the next periods of the resource: at (14,2) with a 50 ms RRI, its
    // second period falls on the reserved (19,2)
    SidelinkCommResourcePool::SubframeInfo now, own;
    now.frameNo = 12;
    now.subframeNo = 0;
    own.frameNo = 14;
    own.subframeNo = 2;
    uint32_t by = 0;
    NS_TEST_ASSERT_MSG_EQ (mac->IsPreempted (now, own, 0, 1, 100.0, -110.0, 5, &by, 50.0, 1), false, "first period is free");
    NS_TEST_ASSERT_MSG_EQ (mac->IsPreempted (now, own, 0, 1, 100.0, -110.0, 5, &by, 50.0, 2), true, "the second is taken");
    mac->SetAttribute ("PreemptionPriority", UintegerValue (2));
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 5, 0, 19, -110.0), false, "sl-PreemptionEnable pl2 needs priority 1");
    mac->SetAttribute ("PreemptionPriority", UintegerValue (3));
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 5, 0, 19, -110.0), true, "pl3 lets priority 2 pre-empt");
    // With priority-aware thresholds the pair (2,5) is 9 dB stricter: -90 dBm clears -110 - 9
    mac->SetAttribute ("PriorityAwareSensing", BooleanValue (true));
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 5, 0, 19, -85.0), true, "-90 > -85 - 9");
    NS_TEST_ASSERT_MSG_EQ (Check (mac, 5, 0, 19, -75.0), false, "-90 < -75 - 9");
  }
};

class NrV2XMode2TestSuite : public TestSuite
{
public:
  NrV2XMode2TestSuite ()
    : TestSuite ("nr-v2x-mode2", UNIT)
  {
    AddTestCase (new SubtractFramesTestCase, TestCase::QUICK);
    AddTestCase (new SubframeSubtractionTestCase, TestCase::QUICK);
    AddTestCase (new SelectionTimersTestCase, TestCase::QUICK);
    AddTestCase (new ReselectionCounterTestCase, TestCase::QUICK);
    AddTestCase (new ResidualCsrTestCase, TestCase::QUICK);
    AddTestCase (new PerInstanceSchedulerTestCase, TestCase::QUICK);
    AddTestCase (new AdaptiveReservationTestCase, TestCase::QUICK);
    AddTestCase (new LegacyHackDefaultTestCase, TestCase::QUICK);
    AddTestCase (new MaxTbSizeTestCase, TestCase::QUICK);
    AddTestCase (new RcsSingleContenderTestCase, TestCase::QUICK);
    AddTestCase (new RcsFewContendersAllWinTestCase, TestCase::QUICK);
    AddTestCase (new RcsOneScPerGroupTestCase, TestCase::QUICK);
    AddTestCase (new RcsToneHearingTestCase, TestCase::QUICK);
    AddTestCase (new RcsDrawTestCase, TestCase::QUICK);
    AddTestCase (new RcsSkewedChoiceTestCase, TestCase::QUICK);
    AddTestCase (new RcsUeToneTestCase, TestCase::QUICK);
    AddTestCase (new PriorityThresholdTestCase, TestCase::QUICK);
    AddTestCase (new PreemptionTestCase, TestCase::QUICK);
  }
};

static NrV2XMode2TestSuite g_nrV2XMode2TestSuite;
