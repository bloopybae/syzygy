#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <string>
#include <vector>

namespace syzygy::capture {

enum class LatencyPreset {
  UltraLow,
  Balanced,
  Safe
};

struct CaptureDevice {
  std::string path;
  std::string name;
  std::string driver;
  std::string bus;
  bool supports_streaming{false};
  bool supports_dma_buf{false};
  std::vector<std::string> pixel_formats;
};

std::vector<CaptureDevice> enumerate_devices();

}  // namespace syzygy::capture

