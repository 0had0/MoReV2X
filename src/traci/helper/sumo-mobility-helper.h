/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef SUMO_MOBILITY_HELPER_H
#define SUMO_MOBILITY_HELPER_H

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ns3/node-container.h"
#include "ns3/ptr.h"
#include "ns3/vector.h"
#include "ns3/traci-client.h"

namespace ns3 {

/**
 * Where the vehicles come from. Exactly one of sumoCfg and traceCsv is set.
 *
 * traceCsv is the replay format of tools/fcd2trace.py, one row per vehicle and
 * time step, header optional: time,node_id,x,y,speed. node_id is the 0-based
 * index in the NodeContainer given to InstallParked.
 */
struct SumoMobilityConfig
{
  SumoMobilityConfig ();

  std::string sumoCfg;      //!< .sumocfg path: live coupling over TraCI
  std::string traceCsv;     //!< pre-recorded trace: no SUMO process is started
  std::string binaryPrefix; //!< prepended to "sumo"; empty resolves it from PATH
  std::string extraOptions; //!< appended to the SUMO command line
  double syncInterval;      //!< seconds between position updates
  double warmup;            //!< SUMO seconds simulated before ns-3 time 0
  double parkDelay;         //!< seconds a departed vehicle keeps its node on the road, must exceed the PDB
  uint32_t seed;            //!< SUMO --seed; 0 keeps the value in the .sumocfg
  uint16_t port;            //!< first TraCI port tried
  double altitude;          //!< antenna height written to every node (m)
  bool recycleNodes;        //!< give the node of an arrived vehicle to the next one
  std::vector<std::string> vehicleIds; //!< if set, vehicleIds[i] drives node i
};

/**
 * Connects SUMO to a pool of pre-created nodes.
 *
 * ns-3.22 cannot add nodes once the channel is built, and MoReV2X sizes its
 * channel matrix once in InitChannelMatrix, so every UE exists from the start
 * and waits at a far-away parking position until a vehicle claims it.
 *
 * Call InstallParked before NistLteHelper::InstallUeDevice (it needs a mobility
 * model) and Start once the script can react to the callbacks. The helper must
 * outlive Simulator::Run.
 */
class SumoMobilityHelper
{
public:
  typedef std::function<void (Ptr<Node>, const std::string &)> VehicleCallback;

  explicit SumoMobilityHelper (const SumoMobilityConfig &config);

  void InstallParked (NodeContainer nodes);
  void Start (VehicleCallback onEnter, VehicleCallback onLeave);

  /// \return the TraCI client, null in trace mode
  Ptr<TraciClient> GetTraciClient (void) const;

  uint32_t GetPeakActiveVehicles (void) const;
  /// \return vehicles that found no free node; non-zero means the pool is too small
  uint32_t GetRejectedVehicles (void) const;

  /// \return the parking position of the node at this pool index
  static Vector GetParkingPosition (uint32_t index);
  /// \return true if the position is one returned by GetParkingPosition
  static bool IsParked (const Vector &position);

private:
  struct TraceSample
  {
    double time;
    Vector position;
  };
  typedef std::map<uint32_t, std::vector<TraceSample> > Trace;

  Ptr<Node> VehicleDeparted (std::string id);
  void VehicleArrived (Ptr<Node> node, std::string id);
  void Park (Ptr<Node> node);
  void ParkAndRelease (Ptr<Node> node);
  void EnterFromTrace (uint32_t index);
  void LeaveFromTrace (uint32_t index);
  Trace ReadTrace (void) const;
  void InstallTraceWaypoints (void);
  void StartTraci (void);
  void StartTrace (void);

  SumoMobilityConfig m_config;
  NodeContainer m_nodes;
  std::map<uint32_t, uint32_t> m_poolIndex; //!< node id -> pool index
  std::deque<Ptr<Node> > m_freeNodes;
  Trace m_trace;
  Ptr<TraciClient> m_traci;
  VehicleCallback m_onEnter;
  VehicleCallback m_onLeave;
  uint32_t m_active;
  uint32_t m_peakActive;
  uint32_t m_rejected;
};

} // namespace ns3

#endif /* SUMO_MOBILITY_HELPER_H */
