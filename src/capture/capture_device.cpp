#include "capture/capture_device.hpp"

#include "syzygy/log.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace syzygy::capture {

namespace {

std::string fourcc_to_string(uint32_t code) {
  std::array<char, 5> fourcc{};
  fourcc[0] = static_cast<char>(code & 0xFF);
  fourcc[1] = static_cast<char>((code >> 8) & 0xFF);
  fourcc[2] = static_cast<char>((code >> 16) & 0xFF);
  fourcc[3] = static_cast<char>((code >> 24) & 0xFF);
  return std::string(fourcc.data());
}

}  // namespace

std::vector<CaptureDevice> enumerate_devices() {
  std::vector<CaptureDevice> devices;
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

  for (const auto& node : nodes) {
    int fd = ::open(node.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
      syzygy::log::warn("enumerate_devices: unable to open", node.string(),
                        std::strerror(errno));
      continue;
    }

    v4l2_capability caps{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &caps) != 0) {
      syzygy::log::warn("VIDIOC_QUERYCAP failed for", node.string(),
                        std::strerror(errno));
      ::close(fd);
      continue;
    }

    CaptureDevice device{};
    device.path = node.string();
    device.name = reinterpret_cast<const char*>(caps.card);
    device.driver = reinterpret_cast<const char*>(caps.driver);
    device.bus = reinterpret_cast<const char*>(caps.bus_info);
    device.supports_streaming = (caps.capabilities & V4L2_CAP_STREAMING) != 0;

    if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE ||
        caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
      v4l2_fmtdesc fmt{};
      fmt.type = (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
                     ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                     : V4L2_BUF_TYPE_VIDEO_CAPTURE;

      while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        device.pixel_formats.emplace_back(fourcc_to_string(fmt.pixelformat));
        fmt.index++;
      }

      v4l2_exportbuffer export_buffer{};
      export_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      export_buffer.index = 0;
      if (ioctl(fd, VIDIOC_EXPBUF, &export_buffer) == 0) {
        device.supports_dma_buf = true;
        ::close(export_buffer.fd);
      }
    }

    devices.emplace_back(std::move(device));
    ::close(fd);
  }
  return devices;
}

}  // namespace syzygy::capture

