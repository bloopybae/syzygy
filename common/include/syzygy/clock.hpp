#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>
//
// Lightweight monotonic clock utilities for latency measurements.

#include <chrono>

namespace syzygy::clock {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

inline TimePoint now() noexcept {
  return Clock::now();
}

inline double milliseconds_since(TimePoint start) noexcept {
  const auto delta = Clock::now() - start;
  return std::chrono::duration<double, std::milli>(delta).count();
}

}  // namespace syzygy::clock

