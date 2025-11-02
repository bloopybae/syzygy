#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace syzygy::audio {

class PipeWireController {
 public:
  PipeWireController();
  ~PipeWireController();

  PipeWireController(const PipeWireController&) = delete;
  PipeWireController& operator=(const PipeWireController&) = delete;

  bool start(std::optional<uint32_t> node_id = std::nullopt,
             std::optional<std::string> bus_path = std::nullopt,
             std::optional<std::string> description = std::nullopt,
             uint32_t channels = 2, uint32_t rate = 48000);
  void stop();

  void set_gain(float gain);
  float gain() const noexcept { return gain_; }

  bool is_running() const noexcept;
  uint32_t sample_rate() const noexcept;
  uint32_t channels() const noexcept;

  float peak_level() const noexcept { return peak_level_.load(); }

 private:
  struct Impl;
  Impl* impl_{nullptr};

  std::atomic<float> peak_level_{0.0f};
  float gain_{1.0f};
};

}  // namespace syzygy::audio
