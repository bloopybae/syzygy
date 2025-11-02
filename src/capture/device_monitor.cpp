#include "capture/device_monitor.hpp"

#include "syzygy/log.hpp"

#include <libudev.h>

namespace syzygy::capture {

DeviceMonitor::DeviceMonitor(Callback callback)
    : callback_(std::move(callback)) {
  running_ = true;
  thread_ = std::thread([this]() { run(); });
}

DeviceMonitor::~DeviceMonitor() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void DeviceMonitor::run() {
#ifdef SYZYGY_HAVE_UDEV
  udev* udev_ctx = udev_new();
  if (!udev_ctx) {
    syzygy::log::warn("DeviceMonitor: unable to create udev context");
    return;
  }

  udev_monitor* monitor =
      udev_monitor_new_from_netlink(udev_ctx, "udev");
  if (!monitor) {
    syzygy::log::warn("DeviceMonitor: unable to create monitor");
    udev_unref(udev_ctx);
    return;
  }

  udev_monitor_filter_add_match_subsystem_devtype(
      monitor, "video4linux", nullptr);
  udev_monitor_enable_receiving(monitor);

  const int fd = udev_monitor_get_fd(monitor);

  while (running_) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    const int ret = select(fd + 1, &fds, nullptr, nullptr, &tv);
    if (ret > 0 && FD_ISSET(fd, &fds)) {
      udev_device* device = udev_monitor_receive_device(monitor);
      if (device) {
        const char* action = udev_device_get_action(device);
        if (action) {
          syzygy::log::info("DeviceMonitor event:", action,
                            udev_device_get_devnode(device));
        }
        if (callback_) {
          callback_();
        }
        udev_device_unref(device);
      }
    }
  }

  udev_monitor_unref(monitor);
  udev_unref(udev_ctx);
#else
  // Fallback: no udev support; nothing to monitor.
  (void)callback_;
#endif
}

}  // namespace syzygy::capture

