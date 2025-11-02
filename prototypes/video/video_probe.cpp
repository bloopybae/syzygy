#include "syzygy/log.hpp"
#include "syzygy/scope_timer.hpp"

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>

namespace {

std::string fourcc_to_string(uint32_t code) {
  std::array<char, 5> fourcc{};
  fourcc[0] = static_cast<char>(code & 0xFF);
  fourcc[1] = static_cast<char>((code >> 8) & 0xFF);
  fourcc[2] = static_cast<char>((code >> 16) & 0xFF);
  fourcc[3] = static_cast<char>((code >> 24) & 0xFF);
  return std::string(fourcc.data());
}

std::string ioctl_error(std::string_view label) {
  std::ostringstream oss;
  oss << label << " failed: " << std::strerror(errno);
  return oss.str();
}

std::vector<std::filesystem::path> enumerate_video_nodes() {
  std::vector<std::filesystem::path> nodes;
  for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
    if (!entry.is_character_file()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (name.rfind("video", 0) == 0) {
      nodes.push_back(entry.path());
    }
  }
  std::sort(nodes.begin(), nodes.end());
  return nodes;
}

void inspect_formats(int fd) {
  v4l2_fmtdesc fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

  while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
    syzygy::log::info("  Format", fmt.index, fourcc_to_string(fmt.pixelformat),
                      "-", reinterpret_cast<char*>(fmt.description));

    v4l2_frmsizeenum frmsize{};
    frmsize.pixel_format = fmt.pixelformat;
    while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
      if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        syzygy::log::info("    Size", frmsize.discrete.width, "x",
                          frmsize.discrete.height);
      } else if (frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
        syzygy::log::info("    Size continuous range");
      }
      frmsize.index++;
    }

    fmt.index++;
  }
}

void inspect_dv_timings(int fd) {
  v4l2_dv_timings timings{};
  if (ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &timings) == 0) {
    const auto& bt = timings.bt;
    const uint32_t htotal =
        bt.width + bt.hfrontporch + bt.hsync + bt.hbackporch;
    const uint32_t vtotal =
        bt.height + bt.vfrontporch + bt.vsync + bt.vbackporch;

    double refresh_hz = 0.0;
    if (htotal > 0 && vtotal > 0) {
      refresh_hz =
          static_cast<double>(bt.pixelclock) / static_cast<double>(htotal * vtotal);
    }
    syzygy::log::info("  DV timings:", bt.width, "x", bt.height, "@",
                      std::fixed, std::setprecision(2), refresh_hz, "Hz");
  } else {
    syzygy::log::warn("  DV timings query unavailable");
  }
}

void inspect_device(const std::filesystem::path& node) {
  syzygy::profiling::ScopeTimer timer{"Inspect " + node.string()};

  const auto fd = ::open(node.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    syzygy::log::warn("Unable to open", node.string(), std::strerror(errno));
    return;
  }

  v4l2_capability caps{};
  if (ioctl(fd, VIDIOC_QUERYCAP, &caps) != 0) {
    syzygy::log::warn(ioctl_error("VIDIOC_QUERYCAP"));
    ::close(fd);
    return;
  }

  syzygy::log::info("Device", node.string(), "-",
                    reinterpret_cast<const char*>(caps.card));
  syzygy::log::info("  Driver:", reinterpret_cast<const char*>(caps.driver));
  syzygy::log::info("  Bus info:", reinterpret_cast<const char*>(caps.bus_info));

  if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
    syzygy::log::info("  Supports single-planar capture");
  }
  if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
    syzygy::log::info("  Supports multi-planar capture");
  }
  if (caps.capabilities & V4L2_CAP_STREAMING) {
    syzygy::log::info("  Supports streaming I/O");
  }
  if (caps.capabilities & V4L2_CAP_EXT_PIX_FORMAT) {
    syzygy::log::info("  Supports extended pixel formats");
  }
  if (caps.capabilities & V4L2_CAP_META_CAPTURE) {
    syzygy::log::info("  Supports metadata capture");
  }
  if (caps.capabilities & V4L2_CAP_DEVICE_CAPS) {
    syzygy::log::info("  Device-specific capabilities detected");
  }
  if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
    inspect_formats(fd);
  }

  inspect_dv_timings(fd);

  v4l2_exportbuffer export_buffer{};
  export_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  export_buffer.index = 0;
  if (ioctl(fd, VIDIOC_EXPBUF, &export_buffer) == 0) {
    syzygy::log::info("  DMA-BUF export supported");
    ::close(export_buffer.fd);
  } else {
    syzygy::log::warn("  DMA-BUF export not available:", std::strerror(errno));
  }

  ::close(fd);
}

}  // namespace

int main() {
  const auto nodes = enumerate_video_nodes();
  if (nodes.empty()) {
    syzygy::log::warn("No /dev/video* nodes detected.");
    return 1;
  }

  for (const auto& node : nodes) {
    inspect_device(node);
  }

  return 0;
}
