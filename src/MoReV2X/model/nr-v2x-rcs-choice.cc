/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "nr-v2x-rcs-choice.h"

#include <ns3/log.h>
#include <ns3/abort.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NrV2XRcsChoice");
NS_OBJECT_ENSURE_REGISTERED (NrV2XRcsChoice);

NrV2XRcsPacket::NrV2XRcsPacket ()
  : priority (0),
    pdbMs (0.0),
    waitedMs (0.0),
    contention (1)
{
}

TypeId
NrV2XRcsChoice::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NrV2XRcsChoice")
    .SetParent<Object> ()
    .AddConstructor<NrV2XRcsChoice> ()
  ;
  return tid;
}

NrV2XRcsChoice::NrV2XRcsChoice ()
{
  m_rng = CreateObject<UniformRandomVariable> ();
}

NrV2XRcsChoice::~NrV2XRcsChoice ()
{
}

void
NrV2XRcsChoice::DoDispose (void)
{
  m_rng = 0;
  Object::DoDispose ();
}

std::vector<double>
NrV2XRcsChoice::GroupDistribution (uint32_t groups, const NrV2XRcsPacket &packet) const
{
  return std::vector<double> (groups, 1.0);
}

std::vector<double>
NrV2XRcsChoice::ToneDistribution (uint32_t subcarriers, uint32_t round, const NrV2XRcsPacket &packet) const
{
  return std::vector<double> (subcarriers, 1.0);
}

uint32_t
NrV2XRcsChoice::ChooseGroup (uint32_t groups, const NrV2XRcsPacket &packet)
{
  std::vector<double> weights = GroupDistribution (groups, packet);
  NS_ABORT_MSG_IF (weights.size () != groups, "RCS: GroupDistribution needs one weight per group");
  return Draw (weights);
}

uint32_t
NrV2XRcsChoice::ChooseTone (uint32_t subcarriers, uint32_t round, const NrV2XRcsPacket &packet)
{
  std::vector<double> weights = ToneDistribution (subcarriers, round, packet);
  NS_ABORT_MSG_IF (weights.size () != subcarriers, "RCS: ToneDistribution needs one weight per sub-carrier");
  return Draw (weights);
}

uint32_t
NrV2XRcsChoice::Draw (const std::vector<double> &weights)
{
  NS_ABORT_MSG_IF (weights.empty (), "RCS: nothing to draw from");
  double total = 0.0;
  bool uniform = true;
  for (uint32_t i = 0; i < weights.size (); i++)
    {
      NS_ABORT_MSG_IF (weights[i] < 0.0, "RCS: negative weight " << weights[i]);
      total += weights[i];
      uniform = uniform && weights[i] == weights[0];
    }
  NS_ABORT_MSG_IF (total <= 0.0, "RCS: all weights are 0");
  if (uniform)
    {
      // Exact, and one integer draw as before the distributions were exposed
      return m_rng->GetInteger (0, weights.size () - 1);
    }
  double u = m_rng->GetValue (0.0, total);
  double cumulative = 0.0;
  for (uint32_t i = 0; i < weights.size (); i++)
    {
      cumulative += weights[i];
      if (u < cumulative && weights[i] > 0.0)
        {
          return i;
        }
    }
  // u fell on total through rounding: the last index with a positive weight
  uint32_t last = weights.size () - 1;
  while (weights[last] <= 0.0)
    {
      last--;
    }
  return last;
}

int64_t
NrV2XRcsChoice::AssignStreams (int64_t stream)
{
  m_rng->SetStream (stream);
  return 1;
}

} // namespace ns3
