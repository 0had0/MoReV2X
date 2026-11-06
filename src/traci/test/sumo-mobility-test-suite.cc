/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "ns3/mobility-model.h"
#include "ns3/node-container.h"
#include "ns3/simulator.h"
#include "ns3/sumo-mobility-helper.h"
#include "ns3/test.h"

using namespace ns3;

class ParkingPositionTestCase : public TestCase
{
public:
  ParkingPositionTestCase () : TestCase ("Parking positions are distinct and recognised") {}

private:
  virtual void DoRun (void)
  {
    const Vector first = SumoMobilityHelper::GetParkingPosition (0);
    const Vector second = SumoMobilityHelper::GetParkingPosition (1);
    NS_TEST_ASSERT_MSG_EQ ((CalculateDistance (first, second) > 0), true,
                           "two parked nodes share a position, pathloss would be infinite");
    NS_TEST_ASSERT_MSG_EQ (SumoMobilityHelper::IsParked (first), true, "parked node not recognised");
    NS_TEST_ASSERT_MSG_EQ (SumoMobilityHelper::IsParked (Vector (2000, 12, 1.5)), false,
                           "vehicle on the road reported as parked");
  }
};

/**
 * Replays a two-vehicle trace and checks that the entry and exit callbacks fire
 * at the trace times, and that a node is on the road only between them.
 */
class TraceReplayTestCase : public TestCase
{
public:
  TraceReplayTestCase () : TestCase ("Trace replay parks nodes outside their active interval") {}

private:
  struct Sample
  {
    bool parked;
    double x;
  };

  void Enter (Ptr<Node> node, const std::string &)
  {
    m_enter[node->GetId ()] = Simulator::Now ().GetSeconds ();
  }
  void Leave (Ptr<Node> node, const std::string &)
  {
    m_leave[node->GetId ()] = Simulator::Now ().GetSeconds ();
  }
  void Check (Ptr<Node> node, const std::string &label)
  {
    const Vector position = node->GetObject<MobilityModel> ()->GetPosition ();
    Sample sample = { SumoMobilityHelper::IsParked (position), position.x };
    m_samples[label] = sample;
  }

  virtual void DoRun (void)
  {
    const std::string path = CreateTempDirFilename ("trace.csv");
    {
      std::ofstream csv (path.c_str ());
      csv << "time,node_id,x,y,speed\n";
      csv << "1.0,0,100,5,20\n1.5,0,110,5,20\n2.0,0,120,5,20\n";
      csv << "0.0,1,500,5,20\n3.0,1,560,5,20\n";
    }

    NodeContainer nodes;
    nodes.Create (3);

    SumoMobilityConfig config;
    config.traceCsv = path;
    SumoMobilityHelper helper (config);
    helper.InstallParked (nodes);
    helper.Start (MakeBound (&TraceReplayTestCase::Enter), MakeBound (&TraceReplayTestCase::Leave));

    Simulator::Schedule (Seconds (0.5), &TraceReplayTestCase::Check, this, nodes.Get (0),
                         std::string ("before"));
    Simulator::Schedule (Seconds (1.5), &TraceReplayTestCase::Check, this, nodes.Get (0),
                         std::string ("during"));
    Simulator::Schedule (Seconds (2.5), &TraceReplayTestCase::Check, this, nodes.Get (0),
                         std::string ("after"));
    Simulator::Schedule (Seconds (2.5), &TraceReplayTestCase::Check, this, nodes.Get (2),
                         std::string ("unused"));
    Simulator::Stop (Seconds (4));
    Simulator::Run ();

    NS_TEST_ASSERT_MSG_EQ (m_samples["before"].parked, true, "node on the road before its first sample");
    NS_TEST_ASSERT_MSG_EQ (m_samples["during"].parked, false, "node parked while active");
    NS_TEST_ASSERT_MSG_EQ_TOL (m_samples["during"].x, 110.0, 1e-3, "position not interpolated from the trace");
    NS_TEST_ASSERT_MSG_EQ (m_samples["after"].parked, true, "node on the road after its last sample");
    NS_TEST_ASSERT_MSG_EQ (m_samples["unused"].parked, true, "node absent from the trace must stay parked");

    const uint32_t id = nodes.Get (0)->GetId ();
    NS_TEST_ASSERT_MSG_EQ_TOL (m_enter[id], 1.0, 1e-3, "enter callback at the wrong time");
    NS_TEST_ASSERT_MSG_EQ_TOL (m_leave[id], 2.0, 1e-3, "leave callback at the wrong time");
    NS_TEST_ASSERT_MSG_EQ (helper.GetPeakActiveVehicles (), 2, "wrong peak of active vehicles");
    NS_TEST_ASSERT_MSG_EQ (helper.GetRejectedVehicles (), 0, "no vehicle should be rejected");

    Simulator::Destroy ();
    std::remove (path.c_str ());
  }

  SumoMobilityHelper::VehicleCallback MakeBound (void (TraceReplayTestCase::*method) (Ptr<Node>, const std::string &))
  {
    return std::bind (method, this, std::placeholders::_1, std::placeholders::_2);
  }

  std::map<uint32_t, double> m_enter;
  std::map<uint32_t, double> m_leave;
  std::map<std::string, Sample> m_samples;
};

class SumoMobilityTestSuite : public TestSuite
{
public:
  SumoMobilityTestSuite ()
    : TestSuite ("sumo-mobility", UNIT)
  {
    AddTestCase (new ParkingPositionTestCase, TestCase::QUICK);
    AddTestCase (new TraceReplayTestCase, TestCase::QUICK);
  }
};

static SumoMobilityTestSuite g_sumoMobilityTestSuite;
