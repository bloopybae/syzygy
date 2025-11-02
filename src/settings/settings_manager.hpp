#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include "capture/capture_device.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace syzygy::settings {

struct SettingsData {
  std::string last_video_device;
  double audio_gain{1.0};
};

class SettingsManager {
 public:
  SettingsManager();

  const SettingsData& data() const noexcept { return data_; }

  void set_last_video_device(const std::string& device_path);
  void set_audio_gain(double gain);

 private:
  void load();
  void save() const;

  std::filesystem::path config_path_;
  SettingsData data_;
};

}  // namespace syzygy::settings
