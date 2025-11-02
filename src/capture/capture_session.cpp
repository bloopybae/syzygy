#include "capture/capture_session.hpp"

#include "syzygy/clock.hpp"
#include "syzygy/log.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <limits>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

namespace syzygy::capture {

namespace {

bool xioctl(int fd, unsigned long request, void* arg) {
  int r;
  do {
    r = ioctl(fd, request, arg);
  } while (r == -1 && errno == EINTR);
  return r != -1;
}

uint32_t preset_to_buffer_count(LatencyPreset preset) {
  switch (preset) {
    case LatencyPreset::UltraLow:
      return 2;
    case LatencyPreset::Balanced:
      return 4;
    case LatencyPreset::Safe:
    default:
      return 6;
  }
}

}  // namespace

CaptureSession::CaptureSession() = default;

CaptureSession::~CaptureSession() {
  stop();
}

bool CaptureSession::start(const std::string& device_path,
                           LatencyPreset preset) {
  stop();

  device_path_ = device_path;
  preset_ = preset;

  if (!configure_device()) {
    syzygy::log::warn("CaptureSession: configure_device failed for",
                      device_path);
    teardown_buffers();
    return false;
  }

  running_ = true;
  worker_ = std::thread([this]() { streaming_loop(); });
  return true;
}

void CaptureSession::stop() {
  if (!running_) {
    teardown_buffers();
    return;
  }
  running_ = false;
  if (worker_.joinable()) {
    worker_.join();
  }
  teardown_buffers();
}

bool CaptureSession::set_latency_preset(LatencyPreset preset) {
  if (preset == preset_) {
    return true;
  }
  preset_ = preset;
  if (!running_) {
    return true;
  }

  const std::string device = device_path_;
  stop();
  return start(device, preset_);
}

std::optional<Frame> CaptureSession::latest_frame() const {
  std::lock_guard<std::mutex> lock(frame_mutex_);
  if (!frame_ready_) {
    return std::nullopt;
  }
  return latest_frame_;
}

bool CaptureSession::configure_device() {
  fd_ = ::open(device_path_.c_str(), O_RDWR | O_NONBLOCK);
  if (fd_ < 0) {
    syzygy::log::warn("CaptureSession: failed to open", device_path_,
                      std::strerror(errno));
    return false;
  }

  v4l2_capability caps{};
  if (!xioctl(fd_, VIDIOC_QUERYCAP, &caps)) {
    syzygy::log::warn("CaptureSession: VIDIOC_QUERYCAP failed",
                      std::strerror(errno));
    return false;
  }

  struct BestMode {
    uint32_t pixel_format = V4L2_PIX_FMT_YUYV;
    uint32_t width = 0;
    uint32_t height = 0;
    v4l2_fract interval{1, 60};
    double fps = 60.0;
    double score = 0.0;
    bool valid = false;
  } best;

  best.width = width_;
  best.height = height_;
  best.score = static_cast<double>(width_) * static_cast<double>(height_) * best.fps;

  const auto evaluate_mode = [&](uint32_t pixfmt, uint32_t width, uint32_t height,
                                 const v4l2_fract& interval) {
    if (interval.numerator == 0 || interval.denominator == 0) {
      return;
    }
    const double fps = static_cast<double>(interval.denominator) /
                       static_cast<double>(interval.numerator);
    if (fps <= 0.0) {
      return;
    }
    const double area = static_cast<double>(width) * static_cast<double>(height);
    double score = area * fps;
    if (!best.valid || score > best.score) {
      best.valid = true;
      best.pixel_format = pixfmt;
      best.width = width;
      best.height = height;
      best.interval = interval;
      best.fps = fps;
      best.score = score;
    }
  };

  v4l2_fmtdesc fmt_desc{};
  fmt_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  for (fmt_desc.index = 0; ioctl(fd_, VIDIOC_ENUM_FMT, &fmt_desc) == 0;
       fmt_desc.index++) {
    const uint32_t pixfmt = fmt_desc.pixelformat;

    if (pixfmt != V4L2_PIX_FMT_YUYV) {
      continue;
    }

    v4l2_frmsizeenum frmsize{};
    frmsize.pixel_format = pixfmt;
    for (frmsize.index = 0; ioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0;
         frmsize.index++) {
      if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        const uint32_t w = frmsize.discrete.width;
        const uint32_t h = frmsize.discrete.height;

        v4l2_frmivalenum frmival{};
        frmival.pixel_format = pixfmt;
        frmival.width = w;
        frmival.height = h;

        for (frmival.index = 0;
             ioctl(fd_, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0;
             frmival.index++) {
          if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            evaluate_mode(pixfmt, w, h, frmival.discrete);
          } else if (frmival.type == V4L2_FRMIVAL_TYPE_STEPWISE) {
            evaluate_mode(pixfmt, w, h, frmival.stepwise.min);
            evaluate_mode(pixfmt, w, h, frmival.stepwise.max);
          }
        }
      }
    }
  }

  if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    syzygy::log::warn("CaptureSession: device lacks VIDEO_CAPTURE capability");
    return false;
  }
  if (!(caps.capabilities & V4L2_CAP_STREAMING)) {
    syzygy::log::warn("CaptureSession: device lacks STREAMING capability");
    return false;
  }

  if (best.valid) {
    width_ = best.width;
    height_ = best.height;
  }

  v4l2_format fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width_;
  fmt.fmt.pix.height = height_;
  fmt.fmt.pix.pixelformat = best.valid ? best.pixel_format : V4L2_PIX_FMT_YUYV;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (!xioctl(fd_, VIDIOC_S_FMT, &fmt)) {
    syzygy::log::warn("CaptureSession: VIDIOC_S_FMT failed",
                      std::strerror(errno));
    return false;
  }

  width_ = fmt.fmt.pix.width;
  height_ = fmt.fmt.pix.height;

  char fourcc[5]{0};
  std::memcpy(fourcc, &fmt.fmt.pix.pixelformat, 4);
  if (best.valid && best.interval.numerator != 0 && best.interval.denominator != 0) {
    const double fps = static_cast<double>(best.interval.denominator) /
                       static_cast<double>(best.interval.numerator);
    syzygy::log::info("CaptureSession mode", width_, "x", height_, fourcc,
                      "@", fps, "Hz");
  } else {
    syzygy::log::info("CaptureSession mode", width_, "x", height_, fourcc);
  }

  if (best.valid && best.interval.numerator != 0 && best.interval.denominator != 0) {
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe = best.interval;
    parm.parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
    if (!xioctl(fd_, VIDIOC_S_PARM, &parm)) {
      syzygy::log::warn("CaptureSession: VIDIOC_S_PARM failed",
                        std::strerror(errno));
    }
  }

  v4l2_requestbuffers req{};
  req.count = preset_to_buffer_count(preset_);
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (!xioctl(fd_, VIDIOC_REQBUFS, &req)) {
    syzygy::log::warn("CaptureSession: VIDIOC_REQBUFS failed",
                      std::strerror(errno));
    return false;
  }

  buffers_.resize(req.count);
  for (uint32_t i = 0; i < req.count; ++i) {
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (!xioctl(fd_, VIDIOC_QUERYBUF, &buf)) {
      syzygy::log::warn("CaptureSession: VIDIOC_QUERYBUF failed",
                        std::strerror(errno));
      return false;
    }

    void* start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd_, buf.m.offset);
    if (start == MAP_FAILED) {
      syzygy::log::warn("CaptureSession: mmap failed", std::strerror(errno));
      return false;
    }

    buffers_[i].start = start;
    buffers_[i].length = buf.length;

    if (!xioctl(fd_, VIDIOC_QBUF, &buf)) {
      syzygy::log::warn("CaptureSession: VIDIOC_QBUF failed",
                        std::strerror(errno));
      return false;
    }
  }

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (!xioctl(fd_, VIDIOC_STREAMON, &type)) {
    syzygy::log::warn("CaptureSession: VIDIOC_STREAMON failed",
                      std::strerror(errno));
    return false;
  }

  syzygy::log::info("CaptureSession streaming", device_path_, width_, "x",
                    height_, "buffers", req.count);
  return true;
}

void CaptureSession::teardown_buffers() {
  if (fd_ >= 0) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);
  }
  for (auto& buffer : buffers_) {
    if (buffer.start && buffer.length) {
      munmap(buffer.start, buffer.length);
    }
    buffer.start = nullptr;
    buffer.length = 0;
  }
  buffers_.clear();
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

void CaptureSession::streaming_loop() {
  while (running_) {
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    const int poll_result = poll(&pfd, 1, 500);
    if (poll_result < 0) {
      if (errno == EINTR) {
        continue;
      }
      syzygy::log::warn("CaptureSession: poll failed", std::strerror(errno));
      break;
    }
    if (poll_result == 0) {
      continue;
    }

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (!xioctl(fd_, VIDIOC_DQBUF, &buf)) {
      if (errno == EAGAIN) {
        continue;
      }
      syzygy::log::warn("CaptureSession: VIDIOC_DQBUF failed",
                        std::strerror(errno));
      break;
    }

    const auto dq_time = syzygy::clock::now();
    const auto& buffer = buffers_[buf.index];
    const auto* src = static_cast<const uint8_t*>(buffer.start);

    Frame frame{};
    frame.width = width_;
    frame.height = height_;
    frame.capture_time = dq_time;
    frame.dequeue_time = dq_time;
    frame.stride = frame.width * 3;
    frame.rgb.resize(static_cast<size_t>(frame.stride) * frame.height);

    if (buf.timestamp.tv_sec != 0 || buf.timestamp.tv_usec != 0) {
      auto capture_duration = std::chrono::seconds(buf.timestamp.tv_sec) +
                              std::chrono::microseconds(buf.timestamp.tv_usec);
      auto steady_capture = syzygy::clock::TimePoint(
          std::chrono::duration_cast<syzygy::clock::Clock::duration>(
              capture_duration));
      frame.capture_time = steady_capture;
    }

    yuyv_to_rgb(src, frame.rgb.data(), frame.width, frame.height);

    {
      std::lock_guard<std::mutex> lock(frame_mutex_);
      latest_frame_ = std::move(frame);
      frame_ready_ = true;
    }

    if (!xioctl(fd_, VIDIOC_QBUF, &buf)) {
      syzygy::log::warn("CaptureSession: VIDIOC_QBUF failed",
                        std::strerror(errno));
      break;
    }
  }

  running_ = false;
}

void CaptureSession::yuyv_to_rgb(const uint8_t* src, uint8_t* dst,
                                 uint32_t width, uint32_t height) {
  const bool use_bt709 = (width >= 1280 || height >= 720);
  // Coefficients scaled by 256 to keep the integer math fast.
  const int coeff_r_v = use_bt709 ? 459 : 409;
  const int coeff_g_u = use_bt709 ? 55 : 100;
  const int coeff_g_v = use_bt709 ? 136 : 208;
  const int coeff_b_u = use_bt709 ? 541 : 516;

  const size_t pixel_count = static_cast<size_t>(width) * height;
  for (size_t i = 0; i < pixel_count; i += 2) {
    const uint8_t y0 = src[0];
    const uint8_t u = src[1];
    const uint8_t y1 = src[2];
    const uint8_t v = src[3];
    src += 4;

    auto convert = [&](uint8_t y, int16_t u_shift,
                       int16_t v_shift) -> std::array<uint8_t, 3> {
      int c = static_cast<int>(y) - 16;
      if (c < 0) {
        c = 0;
      }
      int d = u_shift;
      int e = v_shift;
      int r = (298 * c + coeff_r_v * e + 128) >> 8;
      int g = (298 * c - coeff_g_u * d - coeff_g_v * e + 128) >> 8;
      int b = (298 * c + coeff_b_u * d + 128) >> 8;
      r = std::clamp(r, 0, 255);
      g = std::clamp(g, 0, 255);
      b = std::clamp(b, 0, 255);
      return {static_cast<uint8_t>(r), static_cast<uint8_t>(g),
              static_cast<uint8_t>(b)};
    };

    const int16_t u_shift = static_cast<int16_t>(u) - 128;
    const int16_t v_shift = static_cast<int16_t>(v) - 128;

    const auto rgb0 = convert(y0, u_shift, v_shift);
    const auto rgb1 = convert(y1, u_shift, v_shift);

    dst[0] = rgb0[0];
    dst[1] = rgb0[1];
    dst[2] = rgb0[2];
    dst[3] = rgb1[0];
    dst[4] = rgb1[1];
    dst[5] = rgb1[2];
    dst += 6;
  }
}

}  // namespace syzygy::capture
