/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Repeated Contention Scheduling (RCS) for NR-V2X Mode 2, see nr-v2x-rcs-contention.h
 */

#include "nr-v2x-rcs-contention.h"

#include <ns3/log.h>
#include <ns3/abort.h>
#include <ns3/simulator.h>
#include <ns3/uinteger.h>
#include <ns3/double.h>
#include <ns3/enum.h>
#include <ns3/string.h>
#include <set>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NrV2XRcsContention");

NS_OBJECT_ENSURE_REGISTERED (NrV2XRcsContention);

Ptr<NrV2XRcsContention> NrV2XRcsContention::s_instance = 0;

TypeId
NrV2XRcsContention::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NrV2XRcsContention")
    .SetParent<Object> ()
    .AddConstructor<NrV2XRcsContention> ()
    .AddAttribute ("Groups", "Number of contention groups J (a divisor of ScPerFrame)",
                   UintegerValue (1),
                   MakeUintegerAccessor (&NrV2XRcsContention::m_groups),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("Rounds", "Number of contention rounds r",
                   UintegerValue (3),
                   MakeUintegerAccessor (&NrV2XRcsContention::m_rounds),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("Subcarriers", "Sub-carriers used for the contention, M",
                   UintegerValue (400),
                   MakeUintegerAccessor (&NrV2XRcsContention::m_subcarriers),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("ScPerFrame", "Sub-channels allocated per frame, K",
                   UintegerValue (36),
                   MakeUintegerAccessor (&NrV2XRcsContention::m_scPerFrame),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("ToneMode", "Who hears a contention tone: everybody, within ToneRange, or above ToneThreshold",
                   EnumValue (TONE_ALL),
                   MakeEnumAccessor (&NrV2XRcsContention::m_toneMode),
                   MakeEnumChecker (TONE_ALL, "All", TONE_RANGE, "Range", TONE_POWER, "Power"))
    .AddAttribute ("ToneRange", "Distance (m) within which a tone is heard, ToneMode Range",
                   DoubleValue (300.0),
                   MakeDoubleAccessor (&NrV2XRcsContention::m_toneRange),
                   MakeDoubleChecker<double> (0.0))
    .AddAttribute ("ToneThreshold", "Received power (dBm) above which a tone is heard, ToneMode Power",
                   DoubleValue (-110.0),
                   MakeDoubleAccessor (&NrV2XRcsContention::m_toneThreshold),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("TonePower", "Transmitted power of a tone (dBm), ToneMode Power",
                   DoubleValue (23.0),
                   MakeDoubleAccessor (&NrV2XRcsContention::m_tonePower),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("OutputPath", "Folder of RcsContentionLog.txt; empty: no log",
                   StringValue (""),
                   MakeStringAccessor (&NrV2XRcsContention::m_outputPath),
                   MakeStringChecker ())
  ;
  return tid;
}

NrV2XRcsContention::NrV2XRcsContention ()
  : m_loss (0)
{
  m_rng = CreateObject<UniformRandomVariable> ();
}

NrV2XRcsContention::~NrV2XRcsContention ()
{
}

void
NrV2XRcsContention::DoDispose (void)
{
  if (m_log.is_open ())
    {
      m_log.close ();
    }
  m_loss = 0;
  m_contenders.clear ();
  m_results.clear ();
  Object::DoDispose ();
}

Ptr<NrV2XRcsContention>
NrV2XRcsContention::Get (void)
{
  if (s_instance == 0)
    {
      s_instance = CreateObject<NrV2XRcsContention> ();
      NS_ABORT_MSG_IF (s_instance->m_scPerFrame % s_instance->m_groups != 0,
                       "RCS: the number of groups J must divide the SCs per frame K");
      Simulator::ScheduleDestroy (&NrV2XRcsContention::Reset);
    }
  return s_instance;
}

void
NrV2XRcsContention::Reset (void)
{
  if (s_instance != 0)
    {
      s_instance->Dispose ();
      s_instance = 0;
    }
}

void
NrV2XRcsContention::SetPropagationLossModel (Ptr<PropagationLossModel> model)
{
  m_loss = model;
}

uint32_t NrV2XRcsContention::GetGroups (void) const { return m_groups; }
uint32_t NrV2XRcsContention::GetRounds (void) const { return m_rounds; }
uint32_t NrV2XRcsContention::GetSubcarriers (void) const { return m_subcarriers; }
uint32_t NrV2XRcsContention::GetScPerFrame (void) const { return m_scPerFrame; }

uint32_t
NrV2XRcsContention::GetSubcarriersPerGroup (void) const
{
  return std::max (1u, m_subcarriers / m_groups);
}

uint32_t
NrV2XRcsContention::GetScPerGroup (void) const
{
  return m_scPerFrame / m_groups;
}

void
NrV2XRcsContention::Register (uint64_t frame, uint32_t nodeId, Ptr<MobilityModel> mobility)
{
  NS_ASSERT_MSG (m_results.find (frame) == m_results.end (), "RCS: frame " << frame << " already resolved");
  Contender c;
  c.nodeId = nodeId;
  c.mobility = mobility;
  c.group = m_rng->GetInteger (0, m_groups - 1);
  m_contenders[frame].push_back (c);
}

bool
NrV2XRcsContention::Hears (Ptr<MobilityModel> listener, Ptr<MobilityModel> talker) const
{
  switch (m_toneMode)
    {
    case TONE_ALL:
      return true;
    case TONE_RANGE:
      return listener->GetDistanceFrom (talker) <= m_toneRange;
    case TONE_POWER:
      NS_ABORT_MSG_IF (m_loss == 0, "RCS ToneMode Power needs SetPropagationLossModel");
      return m_loss->CalcRxPower (m_tonePower, talker, listener) >= m_toneThreshold;
    }
  return true;
}

int32_t
NrV2XRcsContention::GetResult (uint64_t frame, uint32_t nodeId)
{
  if (m_results.find (frame) == m_results.end ())
    {
      Resolve (frame);
    }
  std::map<uint32_t, int32_t> &results = m_results[frame];
  std::map<uint32_t, int32_t>::iterator it = results.find (nodeId);
  NS_ASSERT_MSG (it != results.end (), "RCS: node " << nodeId << " did not contend in frame " << frame);
  int32_t sc = it->second;
  results.erase (it);
  if (results.empty ())
    {
      m_results.erase (frame);  // every contender has read its result
    }
  return sc;
}

void
NrV2XRcsContention::Resolve (uint64_t frame)
{
  std::vector<Contender> contenders = m_contenders[frame];
  m_contenders.erase (frame);
  std::map<uint32_t, int32_t> &results = m_results[frame];
  uint32_t n = contenders.size ();
  uint32_t m = GetSubcarriersPerGroup ();
  uint32_t w = GetScPerGroup ();

  // Who hears whom, once per frame: positions do not change within one contention time
  std::vector<std::vector<bool> > hears (n, std::vector<bool> (n, true));
  if (m_toneMode != TONE_ALL)
    {
      for (uint32_t i = 0; i < n; i++)
        {
          for (uint32_t j = 0; j < n; j++)
            {
              if (i != j && contenders[i].group == contenders[j].group)
                {
                  hears[i][j] = Hears (contenders[i].mobility, contenders[j].mobility);
                }
            }
        }
    }

  std::vector<bool> alive (n, true);
  std::vector<uint32_t> pick (n, 0);
  std::vector<uint32_t> lower (n, 0);
  for (uint32_t round = 0; round < m_rounds; round++)
    {
      for (uint32_t i = 0; i < n; i++)
        {
          if (alive[i])
            {
              pick[i] = m_rng->GetInteger (0, m - 1);
            }
        }
      // Every contender transmits and listens at the same time: decide on this round's picks
      std::vector<bool> stays (n, false);
      for (uint32_t i = 0; i < n; i++)
        {
          if (!alive[i])
            {
              continue;
            }
          std::set<uint32_t> lowerHeard;  // distinct lower sub-carriers heard
          for (uint32_t j = 0; j < n; j++)
            {
              if (j != i && alive[j] && contenders[j].group == contenders[i].group
                  && pick[j] < pick[i] && hears[i][j])
                {
                  lowerHeard.insert (pick[j]);
                }
            }
          lower[i] = lowerHeard.size ();
          stays[i] = lower[i] < w;
        }
      alive = stays;
    }

  // Winners take an SC of their group; count winners per SC for the log
  std::map<int32_t, uint32_t> winnersPerSc;
  uint32_t winners = 0;
  for (uint32_t i = 0; i < n; i++)
    {
      int32_t sc = -1;
      if (alive[i])
        {
          sc = contenders[i].group * w + lower[i];
          winnersPerSc[sc]++;
          winners++;
        }
      results[contenders[i].nodeId] = sc;
    }

  if (!m_outputPath.empty ())
    {
      if (!m_log.is_open ())
        {
          m_log.open ((m_outputPath + "RcsContentionLog.txt").c_str (), std::ios_base::app);
        }
      uint32_t collided = 0;
      for (std::map<int32_t, uint32_t>::iterator it = winnersPerSc.begin (); it != winnersPerSc.end (); it++)
        {
          if (it->second > 1)
            {
              collided++;
            }
        }
      // frame, time, contenders, winners, SCs used, SCs with several winners
      m_log << frame << "," << Simulator::Now ().GetSeconds () << "," << n << "," << winners << ","
            << winnersPerSc.size () << "," << collided << "\n";
    }
  NS_LOG_DEBUG ("RCS frame " << frame << ": " << n << " contenders, " << winners << " winners");
}

} // namespace ns3
