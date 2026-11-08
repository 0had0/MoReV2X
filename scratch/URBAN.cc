// URBAN: the TR 37.885 urban grid (Section 6.1.2) with the urban channel.
//
// Streets run every 433 m along x and every 250 m along y, each with 2 lanes of 3.5 m per
// direction and right-hand traffic. The blocks between them are buildings of 413 x 230 m
// (3 m sidewalks), so a link whose straight line crosses a block is NLOS. Vehicles drive
// straight at --Speed and wrap around the grid edge to keep the density constant; they do not
// turn at intersections. --Buildings replaces the blocks (a file with only a header leaves the
// grid open); --sumoCfg or --mobilityTrace replace the built-in drop.
// Everything but the road is in common/v2x-scenario.cc.
#include "common/v2x-scenario.h"

#include <ns3/core-module.h>
#include <ns3/mobility-module.h>

#include <fstream>

using namespace ns3;

namespace {

const double BLOCK_X = 433.0;   // street spacing along x (m)
const double BLOCK_Y = 250.0;   // street spacing along y (m)
const double LANE_WIDTH = 3.5;
const double STREET_HALF = 10.0; // 2 x 2 lanes of 3.5 m and two 3 m sidewalks: 20 m

class Urban : public V2xScenario
{
public:
  std::string Name (void) const override
  {
    return "urban";
  }

  void AddOptions (CommandLine &cmd) override
  {
    cmd.AddValue ("GridX", "Blocks of the urban grid along x (433 m each)", m_blocksX);
    cmd.AddValue ("GridY", "Blocks of the urban grid along y (250 m each)", m_blocksY);
    cmd.AddValue ("Speed", "Vehicle speed (km/h); TR 37.885 uses 60 and 15", m_speedKmh);
    cmd.AddValue ("Buildings", "Building footprints in place of the blocks, one per row: id,x0 y0 x1 y1 ...",
                  m_buildingsFile);
  }

  double RoadLength (void) const override
  {
    return Width ();
  }

  void ConfigureChannel (const std::string &outputPath) override
  {
    NS_ABORT_MSG_IF (m_blocksX < 1 || m_blocksY < 1, "--GridX and --GridY take at least one block");
    std::string buildings = m_buildingsFile;
    if (buildings.empty ())
      {
        buildings = outputPath + "buildings.csv";
        WriteBlocks (buildings);
      }
    Config::SetDefault ("ns3::NrV2XPropagationLossModel::DecorrDistance", DoubleValue (10.0));
    Config::SetDefault ("ns3::NrV2XPropagationLossModel::Urban", BooleanValue (true));
    Config::SetDefault ("ns3::NrV2XPropagationLossModel::BuildingsFile", StringValue (buildings));
  }

  void SetMetricsArea (void) override
  {
    // The whole grid: wrap-around leaves no edge to cut away
    Config::SetDefault ("ns3::NrV2XSpectrumPhy::MetricsMinX", DoubleValue (-STREET_HALF));
    Config::SetDefault ("ns3::NrV2XSpectrumPhy::MetricsMaxX", DoubleValue (Width () + STREET_HALF));
    Config::SetDefault ("ns3::NrV2XUePhy::CbrMinX", DoubleValue (-STREET_HALF));
    Config::SetDefault ("ns3::NrV2XUePhy::CbrMaxX", DoubleValue (Width () + STREET_HALF));
  }

  void InstallMobility (NodeContainer &vehicles) override
  {
    m_vehicles = vehicles;
    // A lane is drawn in proportion to its length: m_blocksY streets of Width () along x
    // and m_blocksX streets of Height () along y, 4 lanes each
    double alongX = m_blocksY * Width ();
    double alongY = m_blocksX * Height ();
    double speed = m_speedKmh / 3.6;
    const double offsets[] = {-1.5 * LANE_WIDTH, -0.5 * LANE_WIDTH, 0.5 * LANE_WIDTH, 1.5 * LANE_WIDTH};

    Ptr<UniformRandomVariable> draw = CreateObject<UniformRandomVariable> ();
    MobilityHelper mobility;
    mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
    mobility.Install (vehicles);
    for (NodeContainer::Iterator it = vehicles.Begin (); it != vehicles.End (); ++it)
      {
        Ptr<ConstantVelocityMobilityModel> mob = (*it)->GetObject<ConstantVelocityMobilityModel> ();
        double offset = offsets[draw->GetInteger (0, 3)];
        if (draw->GetValue (0, alongX + alongY) < alongX)
          {
            // On a street along x: below its centre line eastbound, above westbound
            double y = draw->GetInteger (0, m_blocksY - 1) * BLOCK_Y + offset;
            mob->SetPosition (Vector (draw->GetValue (0, Width ()), y, 0));
            mob->SetVelocity (Vector (offset < 0 ? speed : -speed, 0, 0));
          }
        else
          {
            // On a street along y: right of its centre line northbound, left southbound
            double x = draw->GetInteger (0, m_blocksX - 1) * BLOCK_X + offset;
            mob->SetPosition (Vector (x, draw->GetValue (0, Height ()), 0));
            mob->SetVelocity (Vector (0, offset > 0 ? speed : -speed, 0));
          }
      }
    Simulator::Schedule (MilliSeconds (100), &Urban::WrapAround, this);
  }

  void Describe (std::ostream &readme) const override
  {
    readme << " --- grid = " << m_blocksX << " x " << m_blocksY << " blocks of " << BLOCK_X << " x " << BLOCK_Y
           << " m (" << Width () << " x " << Height () << " m)" << std::endl;
    readme << " --- speed = " << m_speedKmh << " km/h" << std::endl;
    readme << " --- buildings = " << (m_buildingsFile.empty () ? "the blocks" : m_buildingsFile) << std::endl;
  }

private:
  double Width (void) const
  {
    return m_blocksX * BLOCK_X;
  }

  double Height (void) const
  {
    return m_blocksY * BLOCK_Y;
  }

  /** One building per block, 10 m back from the street centre lines, in the BuildingsFile format */
  void WriteBlocks (const std::string &path) const
  {
    std::ofstream out (path.c_str ());
    out << "building,corners" << std::endl;
    uint32_t id = 0;
    for (uint32_t i = 0; i < m_blocksX; ++i)
      {
        for (uint32_t j = 0; j < m_blocksY; ++j)
          {
            double x0 = i * BLOCK_X + STREET_HALF, x1 = (i + 1) * BLOCK_X - STREET_HALF;
            double y0 = j * BLOCK_Y + STREET_HALF, y1 = (j + 1) * BLOCK_Y - STREET_HALF;
            out << id++ << "," << x0 << " " << y0 << " " << x1 << " " << y0 << " " << x1 << " " << y1
                << " " << x0 << " " << y1 << std::endl;
          }
      }
  }

  /** Every 100 ms, a vehicle past the grid edge comes back at the opposite edge */
  void WrapAround (void)
  {
    for (NodeContainer::Iterator it = m_vehicles.Begin (); it != m_vehicles.End (); ++it)
      {
        Ptr<ConstantVelocityMobilityModel> mob = (*it)->GetObject<ConstantVelocityMobilityModel> ();
        Vector position = mob->GetPosition ();
        Vector velocity = mob->GetVelocity ();
        bool moved = false;
        if (velocity.x != 0 && (position.x < 0 || position.x >= Width ()))
          {
            position.x += position.x < 0 ? Width () : -Width ();
            moved = true;
          }
        if (velocity.y != 0 && (position.y < 0 || position.y >= Height ()))
          {
            position.y += position.y < 0 ? Height () : -Height ();
            moved = true;
          }
        if (moved)
          {
            mob->SetPosition (position);
            mob->SetVelocity (velocity);
          }
      }
    Simulator::Schedule (MilliSeconds (100), &Urban::WrapAround, this);
  }

  uint32_t m_blocksX = 3;   // 1299 m, the TR 37.885 minimum
  uint32_t m_blocksY = 3;   // 750 m
  double m_speedKmh = 60;
  std::string m_buildingsFile;
  NodeContainer m_vehicles;
};

} // namespace

int
main (int argc, char *argv[])
{
  Urban urban;
  return RunV2xScenario (argc, argv, urban);
}
