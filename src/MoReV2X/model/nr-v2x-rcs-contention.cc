/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Repeated Contention Scheduling (RCS) for NR-V2X Mode 2, see nr-v2x-rcs-contention.h
 */

#include "nr-v2x-rcs-contention.h"

#include <ns3/log.h>
#include <ns3/abort.h>
#include <ns3/boolean.h>
#include <ns3/simulator.h>
#include <ns3/uinteger.h>
#include <ns3/double.h>
#include <ns3/enum.h>
#include <ns3/string.h>
#include <algorithm>
#include <cmath>
#include <vector>

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
    .AddAttribute ("ScPerSlot", "Sub-channels in one slot (0 = unconstrained for multi-SC blocks)",
                   UintegerValue (0),
                   MakeUintegerAccessor (&NrV2XRcsContention::m_scPerSlot),
                   MakeUintegerChecker<uint32_t> ())
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
    .AddAttribute ("PriorityAware", "Bias contention subcarrier selection towards lower indices for higher SL priorities",
                   BooleanValue (false),
                   MakeBooleanAccessor (&NrV2XRcsContention::m_priorityAware),
                   MakeBooleanChecker ())
    .AddAttribute ("PrioritySigma", "Normalized spread around the target subcarrier rank for priority-aware RCS",
                   DoubleValue (0.18),
                   MakeDoubleAccessor (&NrV2XRcsContention::m_prioritySigma),
                   MakeDoubleChecker<double> (0.01))
    .AddAttribute ("OutputPath", "Folder of RcsContentionLog.txt; empty: no log",
                   StringValue (""),
                   MakeStringAccessor (&NrV2XRcsContention::m_outputPath),
                   MakeStringChecker ())
  ;
  return tid;
}

NrV2XRcsContention::NrV2XRcsContention ()
  : m_scPerSlot (0),
    m_priorityAware (false),
    m_prioritySigma (0.18),
    m_loss (0)
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

void
NrV2XRcsContention::SetScPerSlot (uint32_t scPerSlot)
{
  m_scPerSlot = scPerSlot;
}

uint32_t
NrV2XRcsContention::GetScPerSlot (void) const
{
  return m_scPerSlot;
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
NrV2XRcsContention::Register (uint64_t frame, uint32_t nodeId, Ptr<MobilityModel> mobility,
                              uint8_t priority, uint16_t neededScs)
{
  NS_ASSERT_MSG (m_results.find (frame) == m_results.end (), "RCS: frame " << frame << " already resolved");
  NS_ABORT_MSG_IF (neededScs == 0, "RCS: neededScs must be positive");
  NS_ABORT_MSG_IF (priority > 8, "RCS: SL priority goes from 0 (unset) or 1 to 8, got " << (uint32_t) priority);
  std::vector<Contender> &list = m_contenders[frame];
  for (std::vector<Contender>::const_iterator it = list.begin (); it != list.end (); ++it)
    {
      NS_ABORT_MSG_IF (it->nodeId == nodeId,
                       "RCS: duplicate registration for node " << nodeId << " in frame " << frame);
    }
  Contender c;
  c.nodeId = nodeId;
  c.mobility = mobility;
  c.group = m_rng->GetInteger (0, m_groups - 1);
  c.priority = priority;
  c.neededScs = neededScs;
  list.push_back (c);
}

void
NrV2XRcsContention::Unregister (uint64_t frame, uint32_t nodeId)
{
  NS_ASSERT_MSG (m_results.find (frame) == m_results.end (), "RCS: frame " << frame << " already resolved");
  std::map<uint64_t, std::vector<Contender> >::iterator frameIt = m_contenders.find (frame);
  NS_ASSERT_MSG (frameIt != m_contenders.end (), "RCS: no contenders registered in frame " << frame);
  std::vector<Contender> &list = frameIt->second;
  for (std::vector<Contender>::iterator it = list.begin (); it != list.end (); ++it)
    {
      if (it->nodeId == nodeId)
        {
          list.erase (it);
          return;
        }
    }
  NS_ASSERT_MSG (false, "RCS: node " << nodeId << " was not registered in frame " << frame);
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

static uint32_t
AlignSlotOffset (uint32_t groupStart, uint32_t pos, uint16_t neededScs, uint32_t scPerSlot)
{
  if (neededScs <= 1 || scPerSlot == 0)
    {
      return pos;
    }
  uint32_t inSlot = (groupStart + pos) % scPerSlot;
  if (inSlot > 0 && inSlot + neededScs > scPerSlot)
    {
      return pos + (scPerSlot - inSlot);
    }
  return pos;
}

uint32_t
NrV2XRcsContention::PickSubcarrier (uint32_t subcarriersPerGroup, uint8_t priority) const
{
  if (!m_priorityAware || priority == 0 || subcarriersPerGroup <= 1)
    {
      return m_rng->GetInteger (0, subcarriersPerGroup - 1);
    }
  double target = (priority - 1.0) / 7.0;
  double invTwoSigmaSq = 0.5 / (m_prioritySigma * m_prioritySigma);
  std::vector<double> cumulative (subcarriersPerGroup);
  double total = 0.0;
  for (uint32_t s = 0; s < subcarriersPerGroup; ++s)
    {
      double x = (s + 0.5) / subcarriersPerGroup;
      double diff = x - target;
      total += std::exp (-diff * diff * invTwoSigmaSq) + 0.01;
      cumulative[s] = total;
    }
  double draw = m_rng->GetValue (0.0, total);
  std::vector<double>::const_iterator it = std::lower_bound (cumulative.begin (), cumulative.end (), draw);
  return (it == cumulative.end ()) ? (subcarriersPerGroup - 1) : (uint32_t) (it - cumulative.begin ());
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

  uint16_t maxNeeded = 1;
  for (uint32_t i = 0; i < n; i++)
    {
      maxNeeded = std::max (maxNeeded, contenders[i].neededScs);
    }

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
  std::vector<uint16_t> subCost (m, 0);
  std::vector<uint32_t> seenStamp (m, 0);
  uint32_t stamp = 0;

  for (uint32_t round = 0; round < m_rounds; round++)
    {
      for (uint32_t i = 0; i < n; i++)
        {
          if (alive[i])
            {
              pick[i] = PickSubcarrier (m, contenders[i].priority);
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
          if (++stamp == 0)
            {
              std::fill (seenStamp.begin (), seenStamp.end (), 0);
              stamp = 1;
            }
          uint32_t g = contenders[i].group;
          uint32_t count = 0;
          for (uint32_t j = 0; j < n; j++)
            {
              if (j != i && alive[j] && contenders[j].group == g && pick[j] < pick[i] && hears[i][j])
                {
                  uint32_t s = pick[j];
                  if (seenStamp[s] != stamp)
                    {
                      seenStamp[s] = stamp;
                      subCost[s] = contenders[j].neededScs;
                      ++count;
                    }
                  else if (contenders[j].neededScs > subCost[s])
                    {
                      subCost[s] = contenders[j].neededScs;
                    }
                }
            }
          if (maxNeeded == 1)
            {
              lower[i] = count;
              stays[i] = count < w;
            }
          else
            {
              uint32_t groupStart = g * w;
              uint32_t pos = 0;
              for (uint32_t s = 0; s < pick[i]; ++s)
                {
                  if (seenStamp[s] == stamp)
                    {
                      pos = AlignSlotOffset (groupStart, pos, subCost[s], m_scPerSlot);
                      pos += subCost[s];
                    }
                }
              pos = AlignSlotOffset (groupStart, pos, contenders[i].neededScs, m_scPerSlot);
              lower[i] = pos;
              stays[i] = (pos + contenders[i].neededScs <= w)
                         && (m_scPerSlot == 0 || contenders[i].neededScs <= m_scPerSlot);
            }
        }
      alive = stays;
    }

  // Winners take an SC (or contiguous intra-slot block) of their group; count winners per SC for the log
  std::map<int32_t, uint32_t> winnersPerSc;
  uint32_t winners = 0;
  for (uint32_t i = 0; i < n; i++)
    {
      int32_t sc = -1;
      if (alive[i])
        {
          sc = contenders[i].group * w + lower[i];
          for (uint16_t k = 0; k < contenders[i].neededScs; ++k)
            {
              winnersPerSc[sc + k]++;
            }
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
