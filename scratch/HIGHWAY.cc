// HIGHWAY: the MoReV2X highway, 5 km and 3 lanes of 4 m per direction, 70 km/h, with the
// TR 37.885 highway channel. --Ideal replaces it with the RCS paper's model: static UEs in a
// square, no shadowing. Everything but the road is in common/v2x-scenario.cc.
#include "common/v2x-scenario.h"

#include <ns3/core-module.h>
#include <ns3/mobility-module.h>

using namespace ns3;

namespace {

class Highway : public V2xScenario
{
public:
  std::string Name (void) const override
  {
    return "highway";
  }

  void AddOptions (CommandLine &cmd) override
  {
    cmd.AddValue ("Ideal", "Static UEs in an IdealSide square without shadowing: everybody hears everybody", m_ideal);
    cmd.AddValue ("IdealSide", "Side of the Ideal square (m)", m_idealSide);
    cmd.AddValue ("HighwayLength", "Length of the highway (m)", m_length);
  }

  double RoadLength (void) const override
  {
    return m_length;
  }

  void ConfigureChannel (const std::string &outputPath) override
  {
    if (m_ideal)
      {
        Config::SetDefault ("ns3::NrV2XPropagationLossModel::Sigma", DoubleValue (0.0));
        Config::SetDefault ("ns3::NrV2XPropagationLossModel::SigmaNLOSv", DoubleValue (0.0));
      }
    Config::SetDefault ("ns3::NrV2XPropagationLossModel::DecorrDistance", DoubleValue (25.0));
  }

  void SetMetricsArea (void) override
  {
    if (m_ideal)
      {
        // The square has no edge to cut away
        Config::SetDefault ("ns3::NrV2XSpectrumPhy::MetricsMinX", DoubleValue (-5e4));
        Config::SetDefault ("ns3::NrV2XSpectrumPhy::MetricsMaxX", DoubleValue (1e7));
        Config::SetDefault ("ns3::NrV2XUePhy::CbrMinX", DoubleValue (-5e4));
        Config::SetDefault ("ns3::NrV2XUePhy::CbrMaxX", DoubleValue (1e7));
      }
  }

  void InstallMobility (NodeContainer &vehicles) override
  {
    MobilityHelper mobilityUE;

    Ptr<UniformRandomVariable> laneNumber = CreateObject<UniformRandomVariable> ();
    Ptr<UniformRandomVariable> Xposition = CreateObject<UniformRandomVariable> ();
    double laneWidth = 4.0;

    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
    if (m_ideal)
      {
        // The paper's model: every UE hears every other one; static, in an IdealSide square
        for (NodeContainer::Iterator L = vehicles.Begin (); L != vehicles.End (); ++L)
          {
            positionAlloc->Add (Vector (Xposition->GetValue (0, m_idealSide), Xposition->GetValue (0, m_idealSide), 0));
          }
        mobilityUE.SetPositionAllocator (positionAlloc);
        mobilityUE.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
        mobilityUE.Install (vehicles);
        return;
      }
    for (NodeContainer::Iterator L = vehicles.Begin (); L != vehicles.End (); ++L)
      {
        double yPos = laneNumber->GetInteger (1, 6) * laneWidth;
        double xPos = Xposition->GetValue (0, m_length);
        positionAlloc->Add (Vector (xPos, yPos, 0));
      }
    mobilityUE.SetPositionAllocator (positionAlloc);
    mobilityUE.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
    mobilityUE.Install (vehicles);

    // Lanes above 13 m go east, the others west
    for (NodeContainer::Iterator L = vehicles.Begin (); L != vehicles.End (); ++L)
      {
        Ptr<Node> node = *L;
        Ptr<MobilityModel> mob = node->GetObject<MobilityModel> ();
        Ptr<ConstantVelocityMobilityModel> VelMob = node->GetObject<ConstantVelocityMobilityModel> ();
        if (mob->GetPosition ().y > 13)
          VelMob->SetVelocity (Vector (19.44, 0, 0));
        else
          VelMob->SetVelocity (Vector (-19.44, 0, 0));
      }
  }

private:
  uint32_t m_length = 5000;
  bool m_ideal = false;
  double m_idealSide = 10.0;  // m
};

} // namespace

int
main (int argc, char *argv[])
{
  Highway highway;
  return RunV2xScenario (argc, argv, highway);
}
