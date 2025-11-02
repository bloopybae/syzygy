#include "syzygy/log.hpp"
#include "syzygy/scope_timer.hpp"

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct InfoFrame {
  std::string eotf;
  std::string colorimetry;
  std::string mastering_luminance;
  std::string max_cll;
};

InfoFrame parse_infoframe(const std::string& content) {
  InfoFrame frame{};

  std::regex eotf_regex(R"(EOTF:\s*(.+))");
  std::regex color_regex(R"(Colorimetry:\s*(.+))");
  std::regex master_regex(R"(Mastering display luminance:\s*(.+))");
  std::regex max_cll_regex(R"(MaxCLL:\s*(.+))");

  std::smatch match;
  if (std::regex_search(content, match, eotf_regex)) {
    frame.eotf = match[1];
  }
  if (std::regex_search(content, match, color_regex)) {
    frame.colorimetry = match[1];
  }
  if (std::regex_search(content, match, master_regex)) {
    frame.mastering_luminance = match[1];
  }
  if (std::regex_search(content, match, max_cll_regex)) {
    frame.max_cll = match[1];
  }

  return frame;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path =
      (argc > 1) ? argv[1]
                 : "/sys/kernel/debug/dri/0/hdmi_infoframe";

  syzygy::profiling::ScopeTimer timer{"HDR infoframe probe"};

  std::ifstream file(path);
  if (!file.is_open()) {
    syzygy::log::warn("Unable to open HDMI infoframe at", path,
                      "- try running as root or enabling debugfs.");
    return 1;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  const auto frame = parse_infoframe(content);

  syzygy::log::info("HDR infoframe source:", path);
  if (!frame.eotf.empty()) {
    syzygy::log::info("  EOTF:", frame.eotf);
  }
  if (!frame.colorimetry.empty()) {
    syzygy::log::info("  Colorimetry:", frame.colorimetry);
  }
  if (!frame.mastering_luminance.empty()) {
    syzygy::log::info("  Mastering luminance:", frame.mastering_luminance);
  }
  if (!frame.max_cll.empty()) {
    syzygy::log::info("  MaxCLL:", frame.max_cll);
  }
  if (frame.eotf.empty() && frame.colorimetry.empty()) {
    syzygy::log::warn("No HDR metadata detected in infoframe.");
  }

  return 0;
}

