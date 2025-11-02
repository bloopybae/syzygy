#include "settings/settings_manager.hpp"

#include "syzygy/log.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace syzygy::settings {

namespace {

std::filesystem::path config_directory() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
    return std::filesystem::path(xdg) / "syzygy";
  }
  if (const char* home = std::getenv("HOME")) {
    return std::filesystem::path(home) / ".config" / "syzygy";
  }
  return std::filesystem::temp_directory_path() / "syzygy";
}

}  // namespace

SettingsManager::SettingsManager() {
  config_path_ = config_directory() / "config.ini";
  load();
}

void SettingsManager::load() {
  std::error_code ec;
  std::filesystem::create_directories(config_path_.parent_path(), ec);

  std::ifstream input(config_path_);
  if (!input.is_open()) {
    return;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto pos = line.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    const std::string key = line.substr(0, pos);
    const std::string value = line.substr(pos + 1);

    if (key == "last_video_device") {
      data_.last_video_device = value;
    } else if (key == "audio_gain") {
      data_.audio_gain = std::stod(value);
    }
  }
}

void SettingsManager::save() const {
  std::ofstream output(config_path_, std::ios::trunc);
  if (!output.is_open()) {
    syzygy::log::warn("SettingsManager: unable to write", config_path_.string());
    return;
  }
  output << "last_video_device=" << data_.last_video_device << "\n";
  output << "audio_gain=" << data_.audio_gain << "\n";
}

void SettingsManager::set_last_video_device(const std::string& device_path) {
  if (data_.last_video_device == device_path) {
    return;
  }
  data_.last_video_device = device_path;
  save();
}

void SettingsManager::set_audio_gain(double gain) {
  if (std::abs(data_.audio_gain - gain) < 1e-6) {
    return;
  }
  data_.audio_gain = gain;
  save();
}

}  // namespace syzygy::settings
