#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>
//
// RAII helper to log the lifetime of a scope.

#include "syzygy/clock.hpp"
#include "syzygy/log.hpp"

#include <string>
#include <utility>

namespace syzygy::profiling {

class ScopeTimer {
 public:
  explicit ScopeTimer(std::string label)
      : label_(std::move(label)), start_(clock::now()) {}

  ~ScopeTimer() {
    const auto elapsed = clock::milliseconds_since(start_);
    log::info("Scope", label_, "took", elapsed, "ms");
  }

 private:
  std::string label_;
  clock::TimePoint start_;
};

}  // namespace syzygy::profiling

