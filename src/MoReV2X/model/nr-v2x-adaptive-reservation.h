/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef NR_V2X_ADAPTIVE_RESERVATION_H
#define NR_V2X_ADAPTIVE_RESERVATION_H

#include <stdint.h>
#include <vector>

namespace ns3 {

/**
 * Congestion control for one UE, based on AdaptiveResourceReservation from
 * vivatttt/MoReV2X. At each reselection the UE moves its RRI and its probability
 * of keeping resources according to the free share of the channel it senses, then
 * pulls the RRI towards the RRIs of its neighbours.
 */
class NrV2XAdaptiveReservation
{
public:
  struct Parameters
  {
    Parameters ();

    double rriMin;   //!< ms
    double rriMax;   //!< ms
    double rriStep;  //!< ms, the RRI stays a multiple of this, so the list of RRIs stays short
    double rriDelta; //!< ms, change of the RRI in one update
    double beta;     //!< step of the keep probability, and weight of the own RRI against neighbours
    double keepMax;  //!< largest keep probability
    double freeLow;  //!< below this free ratio the UE backs off
    double freeHigh; //!< above this free ratio the UE speeds up
  };

  NrV2XAdaptiveReservation ();

  void Configure (const Parameters &parameters);

  /**
   * \param rri the RRI in use, in ms
   * \param freeRatio share of subchannels sensed as free, in [0, 1]
   * \param neighbourRris the RRIs heard from other UEs, in ms
   * \return the next RRI, in ms
   */
  double Update (double rri, double freeRatio, const std::vector<double> &neighbourRris);

  double GetKeepProbability (void) const;

private:
  double Limit (double rri) const;

  Parameters m_parameters;
  double m_keepProbability;
};

} // namespace ns3

#endif /* NR_V2X_ADAPTIVE_RESERVATION_H */
