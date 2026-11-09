/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * This software was developed at the National Institute of Standards and
 * Technology by employees of the Federal Government in the course of
 * their official duties. Pursuant to titleElement 17 Section 105 of the United
 * States Code this software is not subject to copyright protection and
 * is in the public domain.
 * NIST assumes no responsibility whatsoever for its use by other parties,
 * and makes no guarantees, expressed or implied, about its quality,
 * reliability, or any other characteristic.
 * 
 * We would appreciate acknowledgement if the software is used.
 * 
 * NIST ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION AND
 * DISCLAIM ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING
 * FROM THE USE OF THIS SOFTWARE.
 * 
 * Modified by: Luca Lusvarghi <luca.lusvarghi5@unimore.it>
 */

#include <cctype>
#include <fstream>
#include <sstream>
#include "ns3/abort.h"
#include "ns3/boolean.h"
#include "ns3/string.h"
#include "ns3/mobility-model.h"
#include "ns3/double.h"
#include "ns3/mobility-building-info.h"
#include "ns3/enum.h"
#include "ns3/nr-v2x-propagation-loss-model.h"
#include "ns3/building-list.h"
#include <ns3/nr-v2x-ue-net-device.h>
#include "ns3/node-container.h"
#include <fstream>
#include <iostream>

NS_LOG_COMPONENT_DEFINE ("NrV2XPropagationLossModel");

namespace ns3 {

std::map<uint32_t, std::map<uint32_t , NrV2XPropagationLossModel::ChannelModel> > NrV2XPropagationLossModel::ChannelMatrix;

NS_OBJECT_ENSURE_REGISTERED (NrV2XPropagationLossModel);


NrV2XPropagationLossModel::NrV2XPropagationLossModel ()
{
  m_randomUniform->SetAttribute ("Min", DoubleValue (0.0));
  m_randomUniform->SetAttribute ("Max", DoubleValue (1.0));
}

NrV2XPropagationLossModel::~NrV2XPropagationLossModel ()
{
}

TypeId
NrV2XPropagationLossModel::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NrV2XPropagationLossModel")
    
    .SetParent<BuildingsPropagationLossModel> ()
    
    .AddConstructor<NrV2XPropagationLossModel> ()
    
    .AddAttribute ("Frequency",
                   "The propagation frequency [in GHz]",
                   DoubleValue (2.4),
                   MakeDoubleAccessor (&NrV2XPropagationLossModel::m_frequency),
                   MakeDoubleChecker<double> ()) 
    .AddAttribute ("Sigma",
                   "The log-normal shadowing standard deviation",
                   DoubleValue (3.0),
                   MakeDoubleAccessor (&NrV2XPropagationLossModel::m_sigma),
                   MakeDoubleChecker<double> ())   
    .AddAttribute ("SigmaNLOSv",
                   "The log-normal NLOSv shadowing standard deviation",
                   DoubleValue (3.0),
                   MakeDoubleAccessor (&NrV2XPropagationLossModel::m_sigmaNLOSv),
                   MakeDoubleChecker<double> ())  
    .AddAttribute ("DecorrDistance",
                   "The shadowing decorrelation distance",
                   DoubleValue (10.0),
                   MakeDoubleAccessor (&NrV2XPropagationLossModel::m_decorrDistance),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Urban",
                   "Use the TR 37.885 urban grid model: UMi street canyon pathloss, NLOS where buildings "
                   "cut the line between the UEs, LOS or NLOSv elsewhere. False: highway model",
                   BooleanValue (false),
                   MakeBooleanAccessor (&NrV2XPropagationLossModel::m_urban),
                   MakeBooleanChecker ())
    .AddAttribute ("SigmaNLOS",
                   "Shadowing standard deviation of links blocked by buildings (urban only), dB",
                   DoubleValue (4.0),
                   MakeDoubleAccessor (&NrV2XPropagationLossModel::m_sigmaNLOS),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("BuildingsFile",
                   "CSV with one building per row: id, then the corners as 'x0 y0 x1 y1 ...' (the "
                   "buildings.csv of tools/qos/make_traces.py). Empty: no buildings",
                   StringValue (""),
                   MakeStringAccessor (&NrV2XPropagationLossModel::LoadBuildings,
                                       &NrV2XPropagationLossModel::GetBuildingsFile),
                   MakeStringChecker ())
  ;
  return tid;
}


double
NrV2XPropagationLossModel::GetLoss (Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
  NS_LOG_FUNCTION(this);
  uint32_t nodeIdA = a->GetObject<Node>()->GetId();
  uint32_t nodeIdB = b->GetObject<Node>()->GetId();
  double distance = 0.0, pathloss = 0.0;


/*  NS_LOG_INFO("Printing channel models matrix");
  //Print matrix
  for (std::map<uint32_t, std::map<uint32_t , ChannelModel> >::iterator II = NrV2XPropagationLossModel::ChannelMatrix.begin(); II != NrV2XPropagationLossModel::ChannelMatrix.end(); ++II)
    for (std::map<uint32_t , ChannelModel>::iterator IIinner = II->second.begin(); IIinner != II->second.end(); ++IIinner)
      NS_LOG_INFO("(" << II->first << "," << IIinner->first << "): distance = " << IIinner->second.Distance << " m, pathloss = " << IIinner->second.Pathloss << " dB, shadowing = " << IIinner->second.Shadowing << " dB, shadowing NLOSv = " << IIinner->second.ShadowingNLOSv << " dB, LOS = " << IIinner->second.LOS);
  */    
  std::map<uint32_t, std::map<uint32_t , ChannelModel> >::iterator txIT = NrV2XPropagationLossModel::ChannelMatrix.find(nodeIdA);
  if (txIT != NrV2XPropagationLossModel::ChannelMatrix.end())
  {
    std::map<uint32_t , ChannelModel>::iterator rxIT = txIT->second.find(nodeIdB);
    if (rxIT != txIT->second.end())
    {
      distance = rxIT->second.Distance;
      pathloss = rxIT->second.Pathloss;
    }
  }

  NS_LOG_INFO ("Tx node: " << nodeIdA << " Rx node: " << nodeIdB << " Distance: " << distance << " Pathloss " << pathloss << " dB");

  return pathloss;
}


double
NrV2XPropagationLossModel::GetShadowing (Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
  NS_LOG_FUNCTION(this);
  uint32_t nodeIdA = a->GetObject<Node>()->GetId();
  uint32_t nodeIdB = b->GetObject<Node>()->GetId();
  double distance = 0.0, shadowing = 0.0, shadowingNLOSv = 0.0, totalShadowing;
  bool LOS = false;

  std::map<uint32_t, std::map<uint32_t , ChannelModel> >::iterator txIT = NrV2XPropagationLossModel::ChannelMatrix.find(nodeIdA);
  if (txIT != NrV2XPropagationLossModel::ChannelMatrix.end())
  {
    std::map<uint32_t , ChannelModel>::iterator rxIT = txIT->second.find(nodeIdB);
    if (rxIT != txIT->second.end())
    {
       distance = rxIT->second.Distance;
       shadowing = rxIT->second.Shadowing;
       shadowingNLOSv = rxIT->second.ShadowingNLOSv;
       LOS = rxIT->second.LOS;
    }
  }
  totalShadowing = shadowing + shadowingNLOSv;

  NS_LOG_INFO ("Tx node: " << nodeIdA << " Rx node: " << nodeIdB << " Distance: " << distance << " LOS " << LOS << " Shadowing " << shadowing << " Shadowing NLOSv " << shadowingNLOSv << " Total " << totalShadowing);

  return totalShadowing;
}



void 
NrV2XPropagationLossModel::InitChannelMatrix (NodeContainer VehicleUEs)
{
  NS_LOG_FUNCTION(this);
  double shadowingValue, shadowingNLOSv;
  bool LOS;
  double Plos;
  // Save the NodeContainer
  m_UEsContainer = VehicleUEs;
  NS_LOG_INFO("Frequency " << m_frequency << " sigma = " << m_sigma << " sigma NLOSv = " << m_sigmaNLOSv << " Decor. distance = " << m_decorrDistance);
  NS_LOG_INFO("Creating channel models matrix...");

  //Init the channel models
  for (NodeContainer::Iterator L = m_UEsContainer.Begin(); L != m_UEsContainer.End(); ++L)
  {
    Ptr<Node> TxNode = *L;
    uint32_t txID;
    txID = TxNode->GetId ();
    std::map<uint32_t , ChannelModel> tmpColumns;
    for (NodeContainer::Iterator K = m_UEsContainer.Begin(); K != m_UEsContainer.End(); ++K)
    {
      Ptr<Node> RxNode = *K;
      uint32_t rxID;     
      rxID = RxNode->GetId ();
      ChannelModel tmp;
      tmp.Distance = 0.0;
      tmp.Pathloss = 0.0;
      tmp.Shadowing = 0.0;
      tmp.ShadowingNLOSv = 0.0;
      tmp.LOS = false;
      tmp.State = NLOSV_STATE;
      tmpColumns.insert(std::pair<uint32_t , ChannelModel> (rxID,tmp));
    }
    NrV2XPropagationLossModel::ChannelMatrix.insert(std::pair<uint32_t, std::map<uint32_t , ChannelModel> > (txID, tmpColumns));
  }
  NS_LOG_INFO("Done.");

  NS_LOG_INFO("Initializing channel models matrix...");
  for (NodeContainer::Iterator L = m_UEsContainer.Begin(); L != m_UEsContainer.End(); ++L)
  {
    Ptr<Node> TxNode = *L;
    uint32_t txID;
    txID = TxNode->GetId ();
    Ptr<MobilityModel> mobTX = TxNode->GetObject<MobilityModel> ();
    for (NodeContainer::Iterator K = m_UEsContainer.Begin(); K != m_UEsContainer.End(); ++K)
    {
      Ptr<Node> RxNode = *K;
      uint32_t rxID;     
      rxID = RxNode->GetId ();
      if (rxID < txID)
      {      
        Ptr<MobilityModel> mobRX = RxNode->GetObject<MobilityModel> ();
        UpdateLink (NrV2XPropagationLossModel::ChannelMatrix[txID][rxID], mobTX, mobRX, true);
      }
    }
  }
  NS_LOG_INFO("Done.");

  NS_LOG_INFO("Creating symmetric matrix...");
  for (std::map<uint32_t, std::map<uint32_t , ChannelModel> >::iterator II = NrV2XPropagationLossModel::ChannelMatrix.begin(); II != NrV2XPropagationLossModel::ChannelMatrix.end(); ++II)    
    for (std::map<uint32_t , ChannelModel>::iterator IIinner = II->second.begin(); IIinner != II->second.end(); ++IIinner)       
      if (IIinner->first > II->first)         
        NrV2XPropagationLossModel::ChannelMatrix[II->first][IIinner->first] = NrV2XPropagationLossModel::ChannelMatrix[IIinner->first][II->first];
  NS_LOG_INFO("Done.");

  NS_LOG_INFO("Printing channel models matrix");
  //Print matrix
  for (std::map<uint32_t, std::map<uint32_t , ChannelModel> >::iterator II = NrV2XPropagationLossModel::ChannelMatrix.begin(); II != NrV2XPropagationLossModel::ChannelMatrix.end(); ++II)
    for (std::map<uint32_t , ChannelModel>::iterator IIinner = II->second.begin(); IIinner != II->second.end(); ++IIinner)
      NS_LOG_INFO("(" << II->first << "," << IIinner->first << "): distance = " << IIinner->second.Distance << " m, pathloss = " << IIinner->second.Pathloss << " dB, shadowing = " << IIinner->second.Shadowing << " dB, shadowing NLOSv = " << IIinner->second.ShadowingNLOSv << " dB, LOS = " << IIinner->second.LOS);
  
//  std::cin.get();    
  Simulator::Schedule (MilliSeconds (100), &NrV2XPropagationLossModel::UpdateChannelMatrix, this);      
}

void 
NrV2XPropagationLossModel::UpdateChannelMatrix (void)
{
  NS_LOG_FUNCTION(this);
  double shadowingValue, shadowingNLOSv;
  bool LOS;
  double Plos;
  NS_LOG_INFO("Frequency " << m_frequency << " sigma = " << m_sigma << " sigma NLOSv = " << m_sigmaNLOSv << " Decor. distance = " << m_decorrDistance);
  NS_LOG_INFO("Updating channel matrix at " << Simulator::Now().GetSeconds() << " s...");

  for (NodeContainer::Iterator L = m_UEsContainer.Begin(); L != m_UEsContainer.End(); ++L)
  {
    Ptr<Node> TxNode = *L;
    uint32_t txID;
    txID = TxNode->GetId ();
    Ptr<MobilityModel> mobTX = TxNode->GetObject<MobilityModel> ();
    for (NodeContainer::Iterator K = m_UEsContainer.Begin(); K != m_UEsContainer.End(); ++K)
    {
      Ptr<Node> RxNode = *K;
      uint32_t rxID;     
      rxID = RxNode->GetId ();
      if (rxID < txID)
      {      
        Ptr<MobilityModel> mobRX = RxNode->GetObject<MobilityModel> ();
        UpdateLink (NrV2XPropagationLossModel::ChannelMatrix[txID][rxID], mobTX, mobRX, false);
      }
    }
  }  
  NS_LOG_INFO("Done.");

  NS_LOG_INFO("Creating symmetric matrix...");
  for (std::map<uint32_t, std::map<uint32_t , ChannelModel> >::iterator II = NrV2XPropagationLossModel::ChannelMatrix.begin(); II != NrV2XPropagationLossModel::ChannelMatrix.end(); ++II)    
    for (std::map<uint32_t , ChannelModel>::iterator IIinner = II->second.begin(); IIinner != II->second.end(); ++IIinner)       
      if (IIinner->first > II->first)         
        NrV2XPropagationLossModel::ChannelMatrix[II->first][IIinner->first] = NrV2XPropagationLossModel::ChannelMatrix[IIinner->first][II->first];       
  NS_LOG_INFO("Done.");

  NS_LOG_INFO("Printing channel models matrix");
  //Print matrix
  for (std::map<uint32_t, std::map<uint32_t , ChannelModel> >::iterator II = NrV2XPropagationLossModel::ChannelMatrix.begin(); II != NrV2XPropagationLossModel::ChannelMatrix.end(); ++II)
    for (std::map<uint32_t , ChannelModel>::iterator IIinner = II->second.begin(); IIinner != II->second.end(); ++IIinner)
      NS_LOG_INFO("(" << II->first << "," << IIinner->first << "): distance = " << IIinner->second.Distance << " m, pathloss = " << IIinner->second.Pathloss << " dB, shadowing = " << IIinner->second.Shadowing << " dB, shadowing NLOSv = " << IIinner->second.ShadowingNLOSv << " dB, LOS = " << IIinner->second.LOS);

//  std::cin.get();
  Simulator::Schedule (MilliSeconds (100), &NrV2XPropagationLossModel::UpdateChannelMatrix, this);      
}

void
NrV2XPropagationLossModel::UpdateLink (ChannelModel &link, Ptr<MobilityModel> tx, Ptr<MobilityModel> rx, bool first)
{
  double distance = std::max (1.0, tx->GetDistanceFrom (rx));
  bool blocked = m_urban && !m_buildings.empty () && IsBlockedByBuildings (tx->GetPosition (), rx->GetPosition ());
  double sigma = blocked ? m_sigmaNLOS : m_sigma;

  // Log-normal shadowing, correlated over the distance the pair moved (Gudmundson)
  double sample = m_shadowing->GetValue (0.0, sigma * sigma);
  if (!first)
    {
      double moved = std::abs (distance - link.Distance);
      sample = std::exp (-moved / m_decorrDistance) * link.Shadowing
               + std::sqrt (1 - std::exp (-2 * moved / m_decorrDistance)) * sample;
    }
  link.Shadowing = sample;
  link.Distance = distance;

  if (blocked)
    {
      link.State = NLOS_STATE;
      link.LOS = false;
      link.Pathloss = UrbanNlosPathloss (distance, m_frequency);
      link.ShadowingNLOSv = 0;
      return;
    }

  double pLos = m_urban ? UrbanLosProbability (distance) : HighwayLosProbability (distance);
  link.LOS = m_randomUniform->GetValue () <= pLos;
  link.State = link.LOS ? LOS_STATE : NLOSV_STATE;
  link.Pathloss = m_urban ? UrbanLosPathloss (distance, m_frequency) : HighwayLosPathloss (distance, m_frequency);
  if (link.LOS)
    {
      link.ShadowingNLOSv = 0;
    }
  else
    {
      // Vehicles in the way: extra loss with mean 5 + max(0, 15 log10(d) - 41) dB, TR 37.885 6.2.1
      Ptr<NormalRandomVariable> blockage = CreateObject<NormalRandomVariable> ();
      double extra = blockage->GetValue (5 + std::max (0.0, (15 * std::log10 (distance)) - 41), (m_sigmaNLOSv * m_sigmaNLOSv));
      link.ShadowingNLOSv = std::max (0.0, extra);
    }
}

double
NrV2XPropagationLossModel::HighwayLosPathloss (double distance, double frequency)
{
  return 32.4 + 20 * std::log10 (distance) + 20 * std::log10 (frequency);
}

double
NrV2XPropagationLossModel::UrbanLosPathloss (double distance, double frequency)
{
  return 38.77 + 16.7 * std::log10 (distance) + 18.2 * std::log10 (frequency);
}

double
NrV2XPropagationLossModel::UrbanNlosPathloss (double distance, double frequency)
{
  return 36.85 + 30 * std::log10 (distance) + 18.9 * std::log10 (frequency);
}

double
NrV2XPropagationLossModel::UrbanLosProbability (double distance)
{
  return std::min (1.0, 1.05 * std::exp (-0.0114 * distance));
}

double
NrV2XPropagationLossModel::HighwayLosProbability (double distance)
{
  if (distance <= 475)
    {
      return std::min (1.0, 2.1013e-6 * distance * distance - 0.002 * distance + 1.0193);
    }
  return std::max (0.0, 0.54 - 0.001 * (distance - 475));
}

void
NrV2XPropagationLossModel::AddBuilding (const std::vector<Vector> &corners)
{
  if (corners.size () < 3)
    {
      NS_FATAL_ERROR ("A building needs at least 3 corners");
    }
  Vector low = corners[0], high = corners[0];
  for (std::size_t i = 1; i < corners.size (); ++i)
    {
      low.x = std::min (low.x, corners[i].x);
      low.y = std::min (low.y, corners[i].y);
      high.x = std::max (high.x, corners[i].x);
      high.y = std::max (high.y, corners[i].y);
    }
  m_buildings.push_back (corners);
  m_buildingBoxes.push_back (std::make_pair (low, high));
}

uint32_t
NrV2XPropagationLossModel::GetNBuildings (void) const
{
  return m_buildings.size ();
}

static double
Cross (const Vector &o, const Vector &a, const Vector &b)
{
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static int
Orientation (const Vector &o, const Vector &a, const Vector &b)
{
  double v = Cross (o, a, b);
  return (v > 0) ? 1 : ((v < 0) ? -1 : 0);
}

// q lies on segment p-r, given that the three points are collinear
static bool
OnSegment (const Vector &p, const Vector &q, const Vector &r)
{
  return std::min (p.x, r.x) <= q.x && q.x <= std::max (p.x, r.x)
         && std::min (p.y, r.y) <= q.y && q.y <= std::max (p.y, r.y);
}

// Segments p1-p2 and q1-q2 cross or touch
static bool
SegmentsIntersect (const Vector &p1, const Vector &p2, const Vector &q1, const Vector &q2)
{
  int o1 = Orientation (p1, p2, q1), o2 = Orientation (p1, p2, q2);
  int o3 = Orientation (q1, q2, p1), o4 = Orientation (q1, q2, p2);
  if (o1 != o2 && o3 != o4)
    {
      return true;
    }
  return (o1 == 0 && OnSegment (p1, q1, p2)) || (o2 == 0 && OnSegment (p1, q2, p2))
         || (o3 == 0 && OnSegment (q1, p1, q2)) || (o4 == 0 && OnSegment (q1, p2, q2));
}

static bool
InsidePolygon (const Vector &p, const std::vector<Vector> &corners)
{
  bool inside = false;
  for (std::size_t i = 0, j = corners.size () - 1; i < corners.size (); j = i++)
    {
      if (((corners[i].y > p.y) != (corners[j].y > p.y))
          && (p.x < (corners[j].x - corners[i].x) * (p.y - corners[i].y) / (corners[j].y - corners[i].y) + corners[i].x))
        {
          inside = !inside;
        }
    }
  return inside;
}

bool
NrV2XPropagationLossModel::IsBlockedByBuildings (const Vector &a, const Vector &b) const
{
  for (std::size_t n = 0; n < m_buildings.size (); ++n)
    {
      const Vector &low = m_buildingBoxes[n].first, &high = m_buildingBoxes[n].second;
      if (std::max (a.x, b.x) < low.x || std::min (a.x, b.x) > high.x
          || std::max (a.y, b.y) < low.y || std::min (a.y, b.y) > high.y)
        {
          continue;
        }
      const std::vector<Vector> &corners = m_buildings[n];
      if (InsidePolygon (a, corners) || InsidePolygon (b, corners))
        {
          return true;
        }
      for (std::size_t i = 0; i < corners.size (); ++i)
        {
          if (SegmentsIntersect (a, b, corners[i], corners[(i + 1) % corners.size ()]))
            {
              return true;
            }
        }
    }
  return false;
}

void
NrV2XPropagationLossModel::LoadBuildings (std::string path)
{
  m_buildingsFile = path;
  m_buildings.clear ();
  m_buildingBoxes.clear ();
  if (path.empty ())
    {
      return;
    }
  std::ifstream file (path.c_str ());
  if (!file.is_open ())
    {
      NS_FATAL_ERROR ("Buildings file not found: " << path);
    }
  std::string line;
  while (std::getline (file, line))
    {
      std::string::size_type comma = line.find (',');
      if (comma == std::string::npos || !std::isdigit (static_cast<unsigned char> (line[0])))
        {
          continue; // header or empty line
        }
      std::istringstream values (line.substr (comma + 1));
      std::vector<Vector> corners;
      double x, y;
      while (values >> x >> y)
        {
          corners.push_back (Vector (x, y, 0));
        }
      AddBuilding (corners);
    }
  NS_LOG_INFO ("Loaded " << m_buildings.size () << " buildings from " << path);
}

std::string
NrV2XPropagationLossModel::GetBuildingsFile (void) const
{
  return m_buildingsFile;
}

NrV2XPropagationLossModel::LinkState
NrV2XPropagationLossModel::GetLinkState (uint32_t txID, uint32_t rxID) const
{
  std::map<uint32_t, std::map<uint32_t , ChannelModel> >::const_iterator txIT = NrV2XPropagationLossModel::ChannelMatrix.find (txID);
  if (txIT != NrV2XPropagationLossModel::ChannelMatrix.end ())
    {
      std::map<uint32_t , ChannelModel>::const_iterator rxIT = txIT->second.find (rxID);
      if (rxIT != txIT->second.end ())
        {
          return rxIT->second.State;
        }
    }
  return NLOSV_STATE;
}

bool
NrV2XPropagationLossModel::GetLineOfSightState (uint32_t txID, uint32_t rxID)
{
  NS_LOG_FUNCTION(this);
  double distance = 0.0;
  bool LOS = false;

  std::map<uint32_t, std::map<uint32_t , ChannelModel> >::iterator txIT = NrV2XPropagationLossModel::ChannelMatrix.find(txID);
  if (txIT != NrV2XPropagationLossModel::ChannelMatrix.end())
  {
    std::map<uint32_t , ChannelModel>::iterator rxIT = txIT->second.find(rxID);
    if (rxIT != txIT->second.end())
    {
       distance = rxIT->second.Distance;
       LOS = rxIT->second.LOS;
    }
  }

  NS_LOG_INFO ("Tx node: " << txID << " Rx node: " << rxID << " Distance: " << distance << " LOS " << LOS);

  return LOS;
}



} // namespace ns3
