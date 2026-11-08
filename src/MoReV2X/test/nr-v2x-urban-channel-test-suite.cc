/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
// TR 37.885 urban grid channel of NrV2XPropagationLossModel: formulas, building geometry and
// the link state the channel matrix assigns.
#include <cstdio>
#include <fstream>

#include "ns3/boolean.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/double.h"
#include "ns3/node-container.h"
#include "ns3/nr-v2x-propagation-loss-model.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/test.h"

using namespace ns3;

namespace {

std::vector<Vector>
Square (double x0, double y0, double x1, double y1)
{
  return {Vector (x0, y0, 0), Vector (x1, y0, 0), Vector (x1, y1, 0), Vector (x0, y1, 0)};
}

NodeContainer
TwoNodes (Vector a, Vector b)
{
  NodeContainer nodes;
  nodes.Create (2);
  Ptr<ConstantPositionMobilityModel> ma = CreateObject<ConstantPositionMobilityModel> ();
  Ptr<ConstantPositionMobilityModel> mb = CreateObject<ConstantPositionMobilityModel> ();
  ma->SetPosition (a);
  mb->SetPosition (b);
  nodes.Get (0)->AggregateObject (ma);
  nodes.Get (1)->AggregateObject (mb);
  return nodes;
}

} // namespace

class UrbanFormulasTestCase : public TestCase
{
public:
  UrbanFormulasTestCase () : TestCase ("TR 37.885 pathloss and LOS probability formulas") {}
private:
  virtual void DoRun (void)
  {
    // 100 m at 5.9 GHz, log10(5.9) = 0.770852
    NS_TEST_ASSERT_MSG_EQ_TOL (NrV2XPropagationLossModel::UrbanLosPathloss (100, 5.9), 38.77 + 33.4 + 18.2 * 0.770852, 1e-4, "urban LOS");
    NS_TEST_ASSERT_MSG_EQ_TOL (NrV2XPropagationLossModel::UrbanNlosPathloss (100, 5.9), 36.85 + 60 + 18.9 * 0.770852, 1e-4, "urban NLOS");
    NS_TEST_ASSERT_MSG_EQ_TOL (NrV2XPropagationLossModel::HighwayLosPathloss (100, 5.9), 32.4 + 40 + 20 * 0.770852, 1e-4, "highway LOS");
    NS_TEST_ASSERT_MSG_EQ_TOL (NrV2XPropagationLossModel::UrbanLosProbability (0), 1.0, 1e-12, "capped at 1");
    NS_TEST_ASSERT_MSG_EQ_TOL (NrV2XPropagationLossModel::UrbanLosProbability (100), 1.05 * std::exp (-1.14), 1e-9, "1.05 exp(-0.0114 d)");
    NS_TEST_ASSERT_MSG_EQ_TOL (NrV2XPropagationLossModel::HighwayLosProbability (600), 0.54 - 0.125, 1e-9, "highway beyond 475 m");
    NS_TEST_ASSERT_MSG_GT (NrV2XPropagationLossModel::UrbanNlosPathloss (200, 5.9),
                           NrV2XPropagationLossModel::UrbanLosPathloss (200, 5.9) + 20, "NLOS is far worse at 200 m");
  }
};

class BuildingGeometryTestCase : public TestCase
{
public:
  BuildingGeometryTestCase () : TestCase ("A building blocks the segments that cross or touch it") {}
private:
  virtual void DoRun (void)
  {
    Ptr<NrV2XPropagationLossModel> model = CreateObject<NrV2XPropagationLossModel> ();
    model->AddBuilding (Square (0, 0, 100, 100));
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (-10, 50, 1.5), Vector (110, 50, 1.5)), true, "straight through");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (-10, -5, 1.5), Vector (110, -5, 1.5)), false, "along the facade");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (-10, 50, 1.5), Vector (50, 110, 1.5)), true, "across a corner");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (-10, 80, 1.5), Vector (20, 120, 1.5)), true, "clips the corner");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (-10, 95, 1.5), Vector (5, 120, 1.5)), false, "passes the corner");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (-10, -10, 1.5), Vector (0, 0, 1.5)), true, "touches the corner");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (50, 50, 1.5), Vector (60, 60, 1.5)), true, "inside");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (150, 0, 1.5), Vector (150, 200, 1.5)), false, "next street");

    // The CSV format of tools/qos/make_traces.py
    std::string path = CreateTempDirFilename ("buildings.csv");
    {
      std::ofstream csv (path.c_str ());
      csv << "building,corners\n0,112.00 112.00 288.00 112.00 288.00 288.00 112.00 288.00\n"
          << "1,312.00 112.00 488.00 112.00 488.00 288.00 312.00 288.00\n";
    }
    model->SetAttribute ("BuildingsFile", StringValue (path));
    NS_TEST_ASSERT_MSG_EQ (model->GetNBuildings (), 2u, "two buildings read, the square above is replaced");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (100, 200, 1.5), Vector (300, 200, 1.5)), true, "into block 0");
    NS_TEST_ASSERT_MSG_EQ (model->IsBlockedByBuildings (Vector (300, 100, 1.5), Vector (300, 300, 1.5)), false, "street between");
    std::remove (path.c_str ());
  }
};

class UrbanLinkStateTestCase : public TestCase
{
public:
  UrbanLinkStateTestCase () : TestCase ("The channel matrix uses NLOS behind buildings and UMi LOS elsewhere") {}
private:
  virtual void DoRun (void)
  {
    NrV2XPropagationLossModel::ChannelMatrix.clear ();
    Ptr<NrV2XPropagationLossModel> model = CreateObject<NrV2XPropagationLossModel> ();
    model->SetAttribute ("Frequency", DoubleValue (5.9));
    model->SetAttribute ("Urban", BooleanValue (true));
    model->AddBuilding (Square (0, 0, 100, 100));

    NodeContainer blocked = TwoNodes (Vector (-10, 50, 1.5), Vector (110, 50, 1.5));
    model->InitChannelMatrix (blocked);
    uint32_t a = blocked.Get (0)->GetId (), b = blocked.Get (1)->GetId ();
    NS_TEST_ASSERT_MSG_EQ (model->GetLinkState (a, b), NrV2XPropagationLossModel::NLOS_STATE, "behind the building");
    NS_TEST_ASSERT_MSG_EQ (model->GetLinkState (b, a), NrV2XPropagationLossModel::NLOS_STATE, "symmetric");
    Ptr<MobilityModel> ma = blocked.Get (0)->GetObject<MobilityModel> ();
    Ptr<MobilityModel> mb = blocked.Get (1)->GetObject<MobilityModel> ();
    NS_TEST_ASSERT_MSG_EQ_TOL (model->GetLoss (ma, mb), NrV2XPropagationLossModel::UrbanNlosPathloss (120, 5.9), 1e-6, "NLOS pathloss");
    Simulator::Destroy ();

    NrV2XPropagationLossModel::ChannelMatrix.clear ();
    NodeContainer street = TwoNodes (Vector (-10, -5, 1.5), Vector (110, -5, 1.5));
    model->InitChannelMatrix (street);
    a = street.Get (0)->GetId ();
    b = street.Get (1)->GetId ();
    NS_TEST_ASSERT_MSG_NE (model->GetLinkState (a, b), NrV2XPropagationLossModel::NLOS_STATE, "same street");
    ma = street.Get (0)->GetObject<MobilityModel> ();
    mb = street.Get (1)->GetObject<MobilityModel> ();
    NS_TEST_ASSERT_MSG_EQ_TOL (model->GetLoss (ma, mb), NrV2XPropagationLossModel::UrbanLosPathloss (120, 5.9), 1e-6, "UMi LOS pathloss");
    Simulator::Destroy ();

    // Highway default: the same pair gets the highway formula and no building is looked at
    NrV2XPropagationLossModel::ChannelMatrix.clear ();
    Ptr<NrV2XPropagationLossModel> highway = CreateObject<NrV2XPropagationLossModel> ();
    highway->SetAttribute ("Frequency", DoubleValue (5.9));
    highway->AddBuilding (Square (0, 0, 100, 100));
    NodeContainer wall = TwoNodes (Vector (-10, 50, 1.5), Vector (110, 50, 1.5));
    highway->InitChannelMatrix (wall);
    a = wall.Get (0)->GetId ();
    b = wall.Get (1)->GetId ();
    NS_TEST_ASSERT_MSG_NE (highway->GetLinkState (a, b), NrV2XPropagationLossModel::NLOS_STATE, "highway ignores buildings");
    ma = wall.Get (0)->GetObject<MobilityModel> ();
    mb = wall.Get (1)->GetObject<MobilityModel> ();
    NS_TEST_ASSERT_MSG_EQ_TOL (highway->GetLoss (ma, mb), NrV2XPropagationLossModel::HighwayLosPathloss (120, 5.9), 1e-6, "highway pathloss");
    Simulator::Destroy ();
    NrV2XPropagationLossModel::ChannelMatrix.clear ();
  }
};

// Over many links in the open the share of LOS links follows the urban LOS probability
class UrbanLosShareTestCase : public TestCase
{
public:
  UrbanLosShareTestCase () : TestCase ("LOS share of unblocked urban links follows TR 37.885") {}
private:
  virtual void DoRun (void)
  {
    NrV2XPropagationLossModel::ChannelMatrix.clear ();
    Ptr<NrV2XPropagationLossModel> model = CreateObject<NrV2XPropagationLossModel> ();
    model->SetAttribute ("Urban", BooleanValue (true));
    // 40 nodes on a line, 10 m apart: pairs at 10..390 m, no buildings
    NodeContainer nodes;
    nodes.Create (40);
    for (uint32_t i = 0; i < nodes.GetN (); ++i)
      {
        Ptr<ConstantPositionMobilityModel> m = CreateObject<ConstantPositionMobilityModel> ();
        m->SetPosition (Vector (10.0 * i, 0, 1.5));
        nodes.Get (i)->AggregateObject (m);
      }
    model->InitChannelMatrix (nodes);
    double expected = 0, observed = 0, pairs = 0;
    for (uint32_t i = 0; i < nodes.GetN (); ++i)
      {
        for (uint32_t j = i + 1; j < nodes.GetN (); ++j)
          {
            double d = 10.0 * (j - i);
            expected += NrV2XPropagationLossModel::UrbanLosProbability (d);
            observed += model->GetLinkState (nodes.Get (i)->GetId (), nodes.Get (j)->GetId ()) == NrV2XPropagationLossModel::LOS_STATE;
            pairs++;
          }
      }
    // 780 Bernoulli draws: 4 standard deviations is about 0.06 of the mean share
    NS_TEST_ASSERT_MSG_EQ_TOL (observed / pairs, expected / pairs, 0.06, "LOS share");
    Simulator::Destroy ();
    NrV2XPropagationLossModel::ChannelMatrix.clear ();
  }
};

class NrV2XUrbanChannelTestSuite : public TestSuite
{
public:
  NrV2XUrbanChannelTestSuite () : TestSuite ("nr-v2x-urban-channel", UNIT)
  {
    AddTestCase (new UrbanFormulasTestCase, TestCase::QUICK);
    AddTestCase (new BuildingGeometryTestCase, TestCase::QUICK);
    AddTestCase (new UrbanLinkStateTestCase, TestCase::QUICK);
    AddTestCase (new UrbanLosShareTestCase, TestCase::QUICK);
  }
};

static NrV2XUrbanChannelTestSuite g_nrV2XUrbanChannelTestSuite;
