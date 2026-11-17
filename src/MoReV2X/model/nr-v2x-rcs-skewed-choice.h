/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * RCS tone distribution skewed by the priority of the packet.
 *
 * A packet of SL priority p (1 highest .. 8; 0, none, counts as 8) draws its tone in each round
 * from a skew-normal density over the relative tone position x = (k + 0.5) / m, k = 0 .. m-1:
 *
 *   f(x) ~ phi(z) Phi(Shape z),   z = (x - (p - 1) / 7) / Scale
 *
 * A priority-1 packet centres on the lowest tones, which win the contention, a priority-8 packet
 * on the highest. Shape > 0 stretches the tail towards the higher tones. The group stays uniform.
 */

#ifndef NR_V2X_RCS_SKEWED_CHOICE_H
#define NR_V2X_RCS_SKEWED_CHOICE_H

#include "nr-v2x-rcs-choice.h"

namespace ns3 {

class NrV2XRcsSkewedChoice : public NrV2XRcsChoice
{
public:
  static TypeId GetTypeId (void);
  NrV2XRcsSkewedChoice ();

  virtual std::vector<double> ToneDistribution (uint32_t subcarriers, uint32_t round, const NrV2XRcsPacket &packet) const;

private:
  double m_scale;  ///< omega, in units of the group's tone range
  double m_shape;  ///< alpha
};

} // namespace ns3

#endif /* NR_V2X_RCS_SKEWED_CHOICE_H */
