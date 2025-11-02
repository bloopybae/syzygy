#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <atomic>
#include <functional>
#include <thread>

namespace syzygy::capture {

class DeviceMonitor {
 public:
  using Callback = std::function<void()>;

  explicit DeviceMonitor(Callback callback);
  ~DeviceMonitor();

  DeviceMonitor(const DeviceMonitor&) = delete;
  DeviceMonitor& operator=(const DeviceMonitor&) = delete;

 private:
  void run();

  Callback callback_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace syzygy::capture

