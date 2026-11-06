/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <list>
#include <map>

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/nist-sl-pool.h"
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
    AddTestCase (new LegacyHackDefaultTestCase, TestCase::QUICK);
  }
};

static NrV2XMode2TestSuite g_nrV2XMode2TestSuite;
