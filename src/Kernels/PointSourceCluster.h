// Copyright (c) 2015-2020 SeisSol Group
// Copyright (C) 2023 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#ifndef KERNELS_POINTSOURCECLUSTER_H_
#define KERNELS_POINTSOURCECLUSTER_H_

#include "Kernels/precision.hpp"
#include "Numerical_aux/Functions.h"
#include "SourceTerm/typedefs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace seissol::kernels {
class PointSourceCluster {
  public:
  virtual ~PointSourceCluster() = default;
  virtual void addTimeIntegratedPointSources(double from, double to) = 0;
  virtual unsigned size() const = 0;
};

/**
 * @brief Integrate sample in time
 *
 * @param from Integration start time
 * @param to Integration end time
 * @param onsetTime Onset time of sample
 * @param samplingInterval Interval length (inverse of sampling rate)
 * @param sample Pointer to sample
 * @param sampleSize Size of the sample
 */
template <typename MathFunctions = seissol::functions::HostStdFunctions>
inline real computeSampleTimeIntegral(double from,
                                      double to,
                                      double const onsetTime,
                                      double const samplingInterval,
                                      real* sample,
                                      std::size_t sampleSize) {
  auto const integrate = [&samplingInterval, &sample](std::size_t index, double tFrom, double tTo) {
    /* We have f(t) = S0 (t1 - t) / dt + S1 (t - t0) / dt, hence
     * int f(t) dt =  S0 (t1 t - 0.5 t^2) / dt + S1 (0.5 t^2 - t0 t) / dt + const, thus
     * int_tFrom^tTo f(t) dt = S0 (t1 (tTo - tFrom) - 0.5 (tTo^2 - tFrom^2)) / dt
     *                       + S1 (0.5 (tTo^2 - tFrom^2) - t0 (tTo - tFrom)) / dt
     */
    auto const t0 = index * samplingInterval;
    auto const t1 = t0 + samplingInterval;
    auto const S0 = sample[index];
    auto const S1 = sample[index + 1];
    auto const tdiff = tTo - tFrom;
    auto const tdiff2 = 0.5 * (tTo * tTo - tFrom * tFrom);
    return (S0 * (t1 * tdiff - tdiff2) + S1 * (tdiff2 - t0 * tdiff)) / samplingInterval;
  };

  if (sampleSize == 0) {
    return 0.0;
  }

  // Shift time such that t = 0 corresponds to onsetTime
  from -= onsetTime;
  to -= onsetTime;
  // Adjust integration interval to sample time interval
  // Sample is implicitly zero outside of sample time interval
  from = MathFunctions::max(from, 0.0);
  to = MathFunctions::min(to, (sampleSize - 1) * samplingInterval);

  // j_{from} := \argmax_j s.t. t_{from} >= j*dt = floor[t_{from} / dt]
  long fromIndex = MathFunctions::floor(from / samplingInterval);
  // j_{to}   := \argmin_j s.t. t_{to}   <= j*dt =  ceil[t_{to}   / dt]
  long toIndex = MathFunctions::ceil(to / samplingInterval);

  fromIndex = MathFunctions::max(0l, fromIndex);
  toIndex = MathFunctions::min(static_cast<long>(sampleSize) - 1, toIndex);
  // Return zero if there is no overlap between integration interval and sample time interval
  if (fromIndex >= toIndex) {
    return 0.0;
  }

  if (toIndex - fromIndex == 1l) {
    return integrate(fromIndex, from, to);
  }

  real integral = 0.0;
  integral += integrate(fromIndex, from, (fromIndex + 1) * samplingInterval);
  for (auto j = fromIndex + 1; j < toIndex - 1; ++j) {
    integral += 0.5 * samplingInterval * (sample[j] + sample[j + 1]);
  }
  integral += integrate(toIndex - 1, (toIndex - 1) * samplingInterval, to);
  return integral;
}

} // namespace seissol::kernels

#endif // KERNELS_POINTSOURCECLUSTER_H_
