// The part of a MoReV2X scenario that does not depend on the road: options, traffic,
// NR-V2X Mode 2 configuration, devices, applications and logs. A scenario (HIGHWAY.cc,
// URBAN.cc) supplies the road through V2xScenario and calls RunV2xScenario from main.
#ifndef MOREV2X_V2X_SCENARIO_H
#define MOREV2X_V2X_SCENARIO_H

#include <ns3/command-line.h>
#include <ns3/node-container.h>

#include <ostream>
#include <string>

namespace ns3 {

class V2xScenario
{
public:
  virtual ~V2xScenario () {}

  /** Short name, written to simREADME.txt */
  virtual std::string Name (void) const = 0;

  /** Adds the options of the road to the command line */
  virtual void AddOptions (CommandLine &cmd) {}

  /** Length along x covered by the frequency-reuse geo-cells (m) */
  virtual double RoadLength (void) const = 0;

  /** Sets the NrV2XPropagationLossModel defaults of the road, before the model is created */
  virtual void ConfigureChannel (const std::string &outputPath) = 0;

  /** Sets the area in which receptions and the CBR are logged, for the built-in drop */
  virtual void SetMetricsArea (void) {}

  /** Places the vehicles of the built-in drop and sets their velocity */
  virtual void InstallMobility (NodeContainer &vehicles) = 0;

  /** Adds the road's lines to simREADME.txt */
  virtual void Describe (std::ostream &readme) const {}
};

/** Runs the scenario: the body of HIGHWAY's main before the road was split out */
int RunV2xScenario (int argc, char *argv[], V2xScenario &scenario);

} // namespace ns3

#endif // MOREV2X_V2X_SCENARIO_H
