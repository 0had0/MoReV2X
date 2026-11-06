/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "nr-v2x-adaptive-reservation.h"

#include <algorithm>
#include <cmath>

namespace ns3 {

NrV2XAdaptiveReservation::Parameters::Parameters ()
  : rriMin (20.0),
    rriMax (100.0),
    rriStep (10.0),
    rriDelta (40.0),
    beta (0.25),
    keepMax (0.8),
    freeLow (0.3),
    freeHigh (0.5)
{
}

NrV2XAdaptiveReservation::NrV2XAdaptiveReservation ()
  : m_keepProbability (0.5)
{
}

void
NrV2XAdaptiveReservation::Configure (const Parameters &parameters)
{
  m_parameters = parameters;
}

double
NrV2XAdaptiveReservation::Limit (double rri) const
{
  const double step = m_parameters.rriStep;
  const double quantised = step * std::floor (rri / step + 0.5);
  return std::max (m_parameters.rriMin, std::min (quantised, m_parameters.rriMax));
}

double
NrV2XAdaptiveReservation::Update (double rri, double freeRatio, const std::vector<double> &neighbourRris)
{
  double next = rri;
  if (freeRatio < m_parameters.freeLow)
    {
      next += m_parameters.rriDelta;
      m_keepProbability = std::max (m_keepProbability - m_parameters.beta, 0.0);
    }
  else if (freeRatio > m_parameters.freeHigh)
    {
      next -= m_parameters.rriDelta;
      m_keepProbability = std::min (m_keepProbability + m_parameters.beta, m_parameters.keepMax);
    }

  next = Limit (next);
  if (!neighbourRris.empty ())
    {
      double sum = 0.0;
      for (std::vector<double>::const_iterator it = neighbourRris.begin (); it != neighbourRris.end (); ++it)
        {
          sum += *it;
        }
      next = m_parameters.beta * next + (1.0 - m_parameters.beta) * sum / neighbourRris.size ();
    }
  return Limit (next);
}

double
NrV2XAdaptiveReservation::GetKeepProbability (void) const
{
  return m_keepProbability;
}

} // namespace ns3
