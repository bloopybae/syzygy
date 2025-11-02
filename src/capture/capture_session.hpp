#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include "capture/capture_device.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace syzygy::capture {

struct Frame {
  uint32_t width{0};
  uint32_t height{0};
  uint32_t stride{0};
  std::vector<uint8_t> rgb;  // RGB24
  std::chrono::steady_clock::time_point capture_time;
  std::chrono::steady_clock::time_point dequeue_time;
};

class CaptureSession {
 public:
  CaptureSession();
  ~CaptureSession();

  CaptureSession(const CaptureSession&) = delete;
  CaptureSession& operator=(const CaptureSession&) = delete;

  bool start(const std::string& device_path, LatencyPreset preset);
  void stop();

  bool set_latency_preset(LatencyPreset preset);
  LatencyPreset latency_preset() const noexcept { return preset_; }

  bool is_running() const noexcept { return running_; }

  std::optional<Frame> latest_frame() const;

 private:
  struct Buffer {
    void* start{nullptr};
    size_t length{0};
  };

  bool configure_device();
  void streaming_loop();
  void teardown_buffers();
  static void yuyv_to_rgb(const uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height);

  std::string device_path_;
  LatencyPreset preset_{LatencyPreset::UltraLow};

  mutable std::mutex frame_mutex_;
  Frame latest_frame_;
  bool frame_ready_{false};

  std::thread worker_;
  std::atomic<bool> running_{false};

  int fd_{-1};
  uint32_t width_{1280};
  uint32_t height_{720};
  std::vector<Buffer> buffers_;
};

}  // namespace syzygy::capture
