/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "nr-v2x-rcs-skewed-choice.h"

#include <ns3/double.h>
#include <cmath>

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (NrV2XRcsSkewedChoice);

TypeId
NrV2XRcsSkewedChoice::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NrV2XRcsSkewedChoice")
    .SetParent<NrV2XRcsChoice> ()
    .AddConstructor<NrV2XRcsSkewedChoice> ()
    .AddAttribute ("Scale", "Scale omega of the skew-normal tone density, as a fraction of the group's tones",
                   DoubleValue (0.25),
                   MakeDoubleAccessor (&NrV2XRcsSkewedChoice::m_scale),
                   MakeDoubleChecker<double> (1e-3))
    .AddAttribute ("Shape", "Shape alpha of the skew-normal tone density; > 0 stretches it towards higher tones",
                   DoubleValue (4.0),
                   MakeDoubleAccessor (&NrV2XRcsSkewedChoice::m_shape),
                   MakeDoubleChecker<double> ())
  ;
  return tid;
}

NrV2XRcsSkewedChoice::NrV2XRcsSkewedChoice ()
  : m_scale (0.25),
    m_shape (4.0)
{
}

std::vector<double>
NrV2XRcsSkewedChoice::ToneDistribution (uint32_t subcarriers, uint32_t round, const NrV2XRcsPacket &packet) const
{
  uint8_t priority = (packet.priority == 0 || packet.priority > 8) ? 8 : packet.priority;
  double location = (priority - 1) / 7.0;
  std::vector<double> distribution (subcarriers);
  for (uint32_t k = 0; k < subcarriers; k++)
    {
      double z = ((k + 0.5) / subcarriers - location) / m_scale;
      double phi = std::exp (-0.5 * z * z);                        // normal density, up to a constant
      double Phi = 0.5 * std::erfc (-m_shape * z / std::sqrt (2.0)); // normal CDF of shape z
      distribution[k] = phi * Phi;
    }
  return distribution;
}

} // namespace ns3
