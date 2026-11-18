// HIGHWAY: the MoReV2X highway, 5 km and 3 lanes of 4 m per direction, 70 km/h, with the
// TR 37.885 highway channel. Everything but the road is in common/v2x-scenario.cc.
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

  double RoadLength (void) const override
  {
    return m_length;
  }

  void ConfigureChannel (const std::string &outputPath) override
  {
    Config::SetDefault ("ns3::NrV2XPropagationLossModel::DecorrDistance", DoubleValue (25.0));
  }

  void InstallMobility (NodeContainer &vehicles) override
  {
    MobilityHelper mobilityUE;

    Ptr<UniformRandomVariable> laneNumber = CreateObject<UniformRandomVariable> ();
    Ptr<UniformRandomVariable> Xposition = CreateObject<UniformRandomVariable> ();
    double laneWidth = 4.0;

    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
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
  double m_length = 5000;
};

} // namespace

int
main (int argc, char *argv[])
{
  Highway highway;
  return RunV2xScenario (argc, argv, highway);
}
