/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "sumo-mobility-helper.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "ns3/abort.h"
#include "ns3/boolean.h"
#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/double.h"
#include "ns3/integer.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/waypoint.h"
#include "ns3/waypoint-mobility-model.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SumoMobilityHelper");

namespace {

const double PARKING_ORIGIN = -1e5; // m, far beyond any sidelink range
const double PARKING_SPACING = 10.0; // m, a pathloss of log10(0) must not happen
const double WAYPOINT_EPSILON = 1e-6; // s, a jump between two waypoints

} // anonymous namespace

SumoMobilityConfig::SumoMobilityConfig ()
  : syncInterval (0.1),
    warmup (0.0),
    parkDelay (0.2),
    seed (0),
    port (3400),
    altitude (1.5),
    recycleNodes (true)
{
}

SumoMobilityHelper::SumoMobilityHelper (const SumoMobilityConfig &config)
  : m_config (config),
    m_active (0),
    m_peakActive (0),
    m_rejected (0)
{
  NS_ABORT_MSG_IF (m_config.sumoCfg.empty () == m_config.traceCsv.empty (),
                   "Set exactly one of sumoCfg and traceCsv");
}

Vector
SumoMobilityHelper::GetParkingPosition (uint32_t index)
{
  return Vector (PARKING_ORIGIN + index * PARKING_SPACING, PARKING_ORIGIN, 0.0);
}

bool
SumoMobilityHelper::IsParked (const Vector &position)
{
  return position.y <= PARKING_ORIGIN + 1.0;
}

void
SumoMobilityHelper::InstallParked (NodeContainer nodes)
{
  m_nodes = nodes;
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      m_poolIndex[nodes.Get (i)->GetId ()] = i;
      m_freeNodes.push_back (nodes.Get (i));
    }

  if (!m_config.traceCsv.empty ())
    {
      InstallTraceWaypoints ();
      return;
    }

  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
  mobility.Install (nodes);
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      Park (nodes.Get (i));
    }
}

void
SumoMobilityHelper::Park (Ptr<Node> node)
{
  Ptr<ConstantVelocityMobilityModel> mobility = node->GetObject<ConstantVelocityMobilityModel> ();
  mobility->SetVelocity (Vector (0.0, 0.0, 0.0));
  mobility->SetPosition (GetParkingPosition (m_poolIndex[node->GetId ()]));
}

void
SumoMobilityHelper::Start (VehicleCallback onEnter, VehicleCallback onLeave)
{
  m_onEnter = onEnter;
  m_onLeave = onLeave;
  if (m_config.traceCsv.empty ())
    {
      StartTraci ();
    }
  else
    {
      StartTrace ();
    }
}

void
SumoMobilityHelper::StartTraci (void)
{
  m_traci = CreateObject<TraciClient> ();
  m_traci->SetAttribute ("SumoConfigPath", StringValue (m_config.sumoCfg));
  m_traci->SetAttribute ("SumoBinaryPath", StringValue (m_config.binaryPrefix));
  m_traci->SetAttribute ("SumoAdditionalCmdOptions", StringValue (m_config.extraOptions));
  m_traci->SetAttribute ("SynchInterval", TimeValue (Seconds (m_config.syncInterval)));
  m_traci->SetAttribute ("StartTime", TimeValue (Seconds (m_config.warmup)));
  m_traci->SetAttribute ("SumoPort", UintegerValue (m_config.port));
  m_traci->SetAttribute ("SumoSeed", IntegerValue (m_config.seed));
  m_traci->SetAttribute ("Altitude", DoubleValue (m_config.altitude));

  STARTUP_FCN include = std::bind (&SumoMobilityHelper::VehicleDeparted, this,
                                   std::placeholders::_1);
  SHUTDOWN_FCN exclude = std::bind (&SumoMobilityHelper::VehicleArrived, this,
                                    std::placeholders::_1, std::placeholders::_2);
  m_traci->SumoSetup (include, exclude);
}

Ptr<Node>
SumoMobilityHelper::VehicleDeparted (std::string id)
{
  Ptr<Node> node;
  if (!m_config.vehicleIds.empty ())
    {
      std::vector<std::string>::const_iterator it =
        std::find (m_config.vehicleIds.begin (), m_config.vehicleIds.end (), id);
      if (it == m_config.vehicleIds.end ())
        {
          return Ptr<Node> ();
        }
      const uint32_t index = static_cast<uint32_t> (it - m_config.vehicleIds.begin ());
      if (index < m_nodes.GetN ())
        {
          node = m_nodes.Get (index);
        }
    }
  else if (!m_freeNodes.empty ())
    {
      node = m_freeNodes.front ();
      m_freeNodes.pop_front ();
    }

  if (!node)
    {
      ++m_rejected;
      NS_LOG_WARN ("No free node for SUMO vehicle " << id);
      return node;
    }

  m_peakActive = std::max (m_peakActive, ++m_active);
  if (m_onEnter)
    {
      m_onEnter (node, id);
    }
  return node;
}

void
SumoMobilityHelper::VehicleArrived (Ptr<Node> node, std::string id)
{
  --m_active;
  if (m_onLeave)
    {
      m_onLeave (node, id);
    }
  // A packet generated just before the vehicle left may still be queued in the MAC
  Simulator::Schedule (Seconds (m_config.parkDelay), &SumoMobilityHelper::ParkAndRelease, this, node);
}

void
SumoMobilityHelper::ParkAndRelease (Ptr<Node> node)
{
  Park (node);
  if (m_config.recycleNodes && m_config.vehicleIds.empty ())
    {
      m_freeNodes.push_back (node);
    }
}

SumoMobilityHelper::Trace
SumoMobilityHelper::ReadTrace (void) const
{
  std::ifstream file (m_config.traceCsv.c_str ());
  NS_ABORT_MSG_IF (!file.is_open (), "Mobility trace not found: " << m_config.traceCsv);

  Trace trace;
  std::string line;
  while (std::getline (file, line))
    {
      const std::string::size_type start = line.find_first_not_of (" \t");
      if (start == std::string::npos || line[start] == '#')
        {
          continue;
        }
      if (!std::isdigit (static_cast<unsigned char> (line[start])))
        {
          continue; // header
        }
      std::replace (line.begin (), line.end (), ',', ' ');
      std::istringstream row (line);
      TraceSample sample;
      uint32_t id;
      double x, y;
      if (!(row >> sample.time >> id >> x >> y))
        {
          NS_ABORT_MSG ("Malformed row in " << m_config.traceCsv << ": " << line);
        }
      sample.position = Vector (x, y, m_config.altitude);
      trace[id].push_back (sample);
    }
  return trace;
}

void
SumoMobilityHelper::InstallTraceWaypoints (void)
{
  m_trace = ReadTrace ();
  for (uint32_t i = 0; i < m_nodes.GetN (); ++i)
    {
      Ptr<Node> node = m_nodes.Get (i);
      Ptr<WaypointMobilityModel> mobility = CreateObject<WaypointMobilityModel> ();
      node->AggregateObject (mobility);

      const Vector parking = GetParkingPosition (i);
      Trace::const_iterator samples = m_trace.find (i);
      if (samples == m_trace.end ())
        {
          mobility->AddWaypoint (Waypoint (Seconds (0), parking));
          continue;
        }

      const double first = samples->second.front ().time;
      if (first > WAYPOINT_EPSILON)
        {
          mobility->AddWaypoint (Waypoint (Seconds (0), parking));
          mobility->AddWaypoint (Waypoint (Seconds (first - WAYPOINT_EPSILON), parking));
        }
      for (std::vector<TraceSample>::const_iterator s = samples->second.begin ();
           s != samples->second.end (); ++s)
        {
          mobility->AddWaypoint (Waypoint (Seconds (s->time), s->position));
        }
      const TraceSample &last = samples->second.back ();
      const double parkTime = last.time + m_config.parkDelay;
      mobility->AddWaypoint (Waypoint (Seconds (parkTime - WAYPOINT_EPSILON), last.position));
      mobility->AddWaypoint (Waypoint (Seconds (parkTime), parking));
    }
}

void
SumoMobilityHelper::StartTrace (void)
{
  for (Trace::const_iterator it = m_trace.begin (); it != m_trace.end (); ++it)
    {
      if (it->first >= m_nodes.GetN ())
        {
          ++m_rejected;
          continue;
        }
      Simulator::Schedule (Seconds (it->second.front ().time),
                           &SumoMobilityHelper::EnterFromTrace, this, it->first);
      Simulator::Schedule (Seconds (it->second.back ().time + 2 * WAYPOINT_EPSILON),
                           &SumoMobilityHelper::LeaveFromTrace, this, it->first);
    }
}

void
SumoMobilityHelper::EnterFromTrace (uint32_t index)
{
  m_peakActive = std::max (m_peakActive, ++m_active);
  if (m_onEnter)
    {
      std::ostringstream id;
      id << "trace" << index;
      m_onEnter (m_nodes.Get (index), id.str ());
    }
}

void
SumoMobilityHelper::LeaveFromTrace (uint32_t index)
{
  --m_active;
  if (m_onLeave)
    {
      std::ostringstream id;
      id << "trace" << index;
      m_onLeave (m_nodes.Get (index), id.str ());
    }
}

Ptr<TraciClient>
SumoMobilityHelper::GetTraciClient (void) const
{
  return m_traci;
}

uint32_t
SumoMobilityHelper::GetPeakActiveVehicles (void) const
{
  return m_peakActive;
}

uint32_t
SumoMobilityHelper::GetRejectedVehicles (void) const
{
  return m_rejected;
}

} // namespace ns3
