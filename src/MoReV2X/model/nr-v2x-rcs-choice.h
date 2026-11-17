/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Repeated Contention Scheduling (RCS): the two random choices of a contender.
 *
 * Before a contention a UE picks one of the J groups, and in each of the r rounds one of
 * the m sub-carriers of that group for its tone. A lower sub-carrier beats a higher one,
 * so the tone distribution sets how likely a UE is to win, and the group distribution
 * how the UEs spread over the SCs of the frame.
 *
 * NrV2XRcsChoice draws both from a probability mass function. GroupDistribution and
 * ToneDistribution return these functions and are uniform here, as in the paper. They
 * receive the packet in contention, so that a subclass can skew them, for example by
 * the priority of the packet (the default priority level of its PQI, TS 23.287 5.4.4).
 */

#ifndef NR_V2X_RCS_CHOICE_H
#define NR_V2X_RCS_CHOICE_H

#include <ns3/object.h>
#include <ns3/ptr.h>
#include <ns3/random-variable-stream.h>
#include <vector>

namespace ns3 {

/// The packet a UE contends for
struct NrV2XRcsPacket
{
  NrV2XRcsPacket ();
  uint8_t priority;     ///< SL priority, 1 (highest) to 8; 0 when the packet has none
  double pdbMs;         ///< packet delay budget
  double waitedMs;      ///< time from generation to the start of this contention
  uint32_t contention;  ///< 1 for the first contention of the packet, 2 for the next, ...
};

class NrV2XRcsChoice : public Object
{
public:
  static TypeId GetTypeId (void);
  NrV2XRcsChoice ();
  virtual ~NrV2XRcsChoice ();

  /**
   * Probability of each group 0 .. groups-1 for this packet. The entries are weights:
   * they need not add up to 1, but at least one must be positive. Uniform here.
   */
  virtual std::vector<double> GroupDistribution (uint32_t groups, const NrV2XRcsPacket &packet) const;

  /**
   * Probability of each sub-carrier 0 .. subcarriers-1 of the group in round `round`
   * (0 .. r-1); sub-carrier 0 is the lowest, the strongest. Weights as above. Uniform here.
   */
  virtual std::vector<double> ToneDistribution (uint32_t subcarriers, uint32_t round, const NrV2XRcsPacket &packet) const;

  /// A group drawn from GroupDistribution
  uint32_t ChooseGroup (uint32_t groups, const NrV2XRcsPacket &packet);
  /// A sub-carrier drawn from ToneDistribution
  uint32_t ChooseTone (uint32_t subcarriers, uint32_t round, const NrV2XRcsPacket &packet);

  /// An index drawn with probability proportional to its weight
  uint32_t Draw (const std::vector<double> &weights);

  /// Set the stream of the random variable; returns the number of streams used (1)
  int64_t AssignStreams (int64_t stream);

protected:
  virtual void DoDispose (void);

private:
  Ptr<UniformRandomVariable> m_rng;
};

} // namespace ns3

#endif /* NR_V2X_RCS_CHOICE_H */
