#include "syzygy/clock.hpp"
#include "syzygy/log.hpp"
#include "syzygy/scope_timer.hpp"

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr uint32_t kBufferCount = 3;

struct Buffer {
  void* start = nullptr;
  size_t length = 0;
  int index = 0;
  int dma_fd = -1;
};

struct VulkanContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
};

struct CaptureContext {
  int fd = -1;
  std::vector<Buffer> buffers;
  bool streamon = false;
};

[[noreturn]] void fatal(const std::string& message) {
  syzygy::log::fatal(message);
}

int open_device(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    fatal("Unable to open " + path + ": " + std::strerror(errno));
  }
  return fd;
}

void query_caps(int fd) {
  v4l2_capability caps{};
  if (ioctl(fd, VIDIOC_QUERYCAP, &caps) != 0) {
    fatal("VIDIOC_QUERYCAP failed: " + std::string(std::strerror(errno)));
  }
  if ((caps.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
    fatal("Device does not support single-planar capture");
  }
  if ((caps.capabilities & V4L2_CAP_STREAMING) == 0) {
    fatal("Device does not support streaming I/O");
  }
  syzygy::log::info("Using capture card:", reinterpret_cast<char*>(caps.card));
}

void set_format(int fd) {
  v4l2_format fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = kWidth;
  fmt.fmt.pix.height = kHeight;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

  if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
    fatal("VIDIOC_S_FMT failed: " + std::string(std::strerror(errno)));
  }

  syzygy::log::info("Negotiated format:",
                    fmt.fmt.pix.width, "x", fmt.fmt.pix.height,
                    "stride", fmt.fmt.pix.bytesperline,
                    "fourcc", fmt.fmt.pix.pixelformat);
}

Buffer create_buffer(int fd, int index) {
  v4l2_buffer buffer{};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  buffer.index = index;

  if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) != 0) {
    fatal("VIDIOC_QUERYBUF failed: " + std::string(std::strerror(errno)));
  }

  void* start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, buffer.m.offset);
  if (start == MAP_FAILED) {
    fatal("mmap failed: " + std::string(std::strerror(errno)));
  }

  v4l2_exportbuffer exp{};
  exp.type = buffer.type;
  exp.index = index;
  exp.flags = O_CLOEXEC;

  if (ioctl(fd, VIDIOC_EXPBUF, &exp) != 0) {
    fatal("VIDIOC_EXPBUF failed: " + std::string(std::strerror(errno)));
  }

  Buffer result{};
  result.start = start;
  result.length = buffer.length;
  result.index = index;
  result.dma_fd = exp.fd;

  return result;
}

void queue_buffer(int fd, int index) {
  v4l2_buffer qbuf{};
  qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  qbuf.memory = V4L2_MEMORY_MMAP;
  qbuf.index = index;

  if (ioctl(fd, VIDIOC_QBUF, &qbuf) != 0) {
    fatal("VIDIOC_QBUF failed: " + std::string(std::strerror(errno)));
  }
}

void start_streaming(CaptureContext& ctx) {
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(ctx.fd, VIDIOC_STREAMON, &type) != 0) {
    fatal("VIDIOC_STREAMON failed: " + std::string(std::strerror(errno)));
  }
  ctx.streamon = true;
}

void stop_streaming(CaptureContext& ctx) {
  if (!ctx.streamon) {
    return;
  }
  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(ctx.fd, VIDIOC_STREAMOFF, &type) != 0) {
    syzygy::log::warn("VIDIOC_STREAMOFF failed:",
                      std::strerror(errno));
  }
  ctx.streamon = false;
}

void cleanup_capture(CaptureContext& ctx) {
  stop_streaming(ctx);
  for (auto& buffer : ctx.buffers) {
    if (buffer.start && buffer.length) {
      munmap(buffer.start, buffer.length);
    }
    if (buffer.dma_fd >= 0) {
      close(buffer.dma_fd);
    }
  }
  ctx.buffers.clear();
  if (ctx.fd >= 0) {
    close(ctx.fd);
    ctx.fd = -1;
  }
}

bool has_extension(VkPhysicalDevice device,
                   const char* name) {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> props(count);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, props.data());
  for (const auto& prop : props) {
    if (std::string_view(prop.extensionName) == name) {
      return true;
    }
  }
  return false;
}

uint32_t find_queue_family(VkPhysicalDevice device) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
  for (uint32_t i = 0; i < count; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return i;
    }
  }
  fatal("No suitable Vulkan queue family found");
  return 0;
}

uint32_t select_memory_type(VkPhysicalDevice device,
                            uint32_t type_bits,
                            VkMemoryPropertyFlags required) {
  VkPhysicalDeviceMemoryProperties properties{};
  vkGetPhysicalDeviceMemoryProperties(device, &properties);
  for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (properties.memoryTypes[i].propertyFlags & required) == required) {
      return i;
    }
  }
  fatal("Unable to find compatible Vulkan memory type");
  return 0;
}

VulkanContext create_vulkan_context() {
  VulkanContext ctx{};

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Syzygy Video DMABUF Prototype";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.pEngineName = "syzygy";
  app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &app_info;

  if (vkCreateInstance(&instance_info, nullptr, &ctx.instance) != VK_SUCCESS) {
    fatal("Failed to create Vulkan instance");
  }

  uint32_t gpu_count = 0;
  vkEnumeratePhysicalDevices(ctx.instance, &gpu_count, nullptr);
  if (gpu_count == 0) {
    fatal("No Vulkan physical devices detected");
  }
  std::vector<VkPhysicalDevice> devices(gpu_count);
  vkEnumeratePhysicalDevices(ctx.instance, &gpu_count, devices.data());

  ctx.physical_device = VK_NULL_HANDLE;
  for (auto device : devices) {
    if (has_extension(device, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) &&
        has_extension(device, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME)) {
      ctx.physical_device = device;
      break;
    }
  }
  if (ctx.physical_device == VK_NULL_HANDLE) {
    fatal("No device exposes required DMA-BUF extensions");
  }

  ctx.queue_family = find_queue_family(ctx.physical_device);
  float queue_priority = 1.0f;

  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = ctx.queue_family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &queue_priority;

  const std::array<const char*, 2> extensions = {
      VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
      VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
  };

  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  device_info.ppEnabledExtensionNames = extensions.data();

  if (vkCreateDevice(ctx.physical_device, &device_info, nullptr, &ctx.device) !=
      VK_SUCCESS) {
    fatal("Failed to create Vulkan device");
  }

  vkGetDeviceQueue(ctx.device, ctx.queue_family, 0, &ctx.queue);
  return ctx;
}

void destroy_vulkan_context(VulkanContext& ctx) {
  if (ctx.device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(ctx.device);
    vkDestroyDevice(ctx.device, nullptr);
    ctx.device = VK_NULL_HANDLE;
  }
  if (ctx.instance != VK_NULL_HANDLE) {
    vkDestroyInstance(ctx.instance, nullptr);
    ctx.instance = VK_NULL_HANDLE;
  }
}

void import_dma_buf(const VulkanContext& ctx, const Buffer& buffer) {
  syzygy::log::info("Importing DMA-BUF fd", buffer.dma_fd,
                    "length", buffer.length, "bytes");

  VkExternalMemoryBufferCreateInfo external_info{};
  external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
  external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.pNext = &external_info;
  buffer_info.size = buffer.length;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkBuffer vk_buffer = VK_NULL_HANDLE;
  if (vkCreateBuffer(ctx.device, &buffer_info, nullptr, &vk_buffer) != VK_SUCCESS) {
    fatal("Failed to create Vulkan buffer");
  }

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(ctx.device, vk_buffer, &requirements);

  VkImportMemoryFdInfoKHR import_info{};
  import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
  import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
  import_info.fd = ::dup(buffer.dma_fd);

  VkMemoryAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.pNext = &import_info;
  alloc_info.allocationSize = requirements.size;
  alloc_info.memoryTypeIndex = select_memory_type(ctx.physical_device,
                                                  requirements.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkDeviceMemory memory = VK_NULL_HANDLE;
  if (vkAllocateMemory(ctx.device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
    fatal("Failed to allocate Vulkan memory for DMA-BUF");
  }

  if (vkBindBufferMemory(ctx.device, vk_buffer, memory, 0) != VK_SUCCESS) {
    fatal("Failed to bind Vulkan buffer memory");
  }

  syzygy::log::info("DMA-BUF import successful; Vulkan buffer ready.");

  vkDestroyBuffer(ctx.device, vk_buffer, nullptr);
  vkFreeMemory(ctx.device, memory, nullptr);
}

void capture_frames(CaptureContext& cap_ctx,
                    const VulkanContext& vk_ctx,
                    int frame_count) {
  const auto start_time = syzygy::clock::now();
  bool imported = false;
  for (int i = 0; i < frame_count; ++i) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cap_ctx.fd, VIDIOC_DQBUF, &buffer) != 0) {
      if (errno == EAGAIN) {
        usleep(1000);
        continue;
      }
      fatal("VIDIOC_DQBUF failed: " + std::string(std::strerror(errno)));
    }

    const auto timestamp_ms =
        buffer.timestamp.tv_sec * 1000.0 +
        buffer.timestamp.tv_usec / 1000.0;

    const double monotonic_ms =
        syzygy::clock::milliseconds_since(start_time);

    syzygy::log::info("Frame", i,
                      "index", buffer.index,
                      "bytes", buffer.bytesused,
                      "driver ts", timestamp_ms, "ms",
                      "monotonic delta", monotonic_ms, "ms");

    if (!imported) {
      import_dma_buf(vk_ctx, cap_ctx.buffers[buffer.index]);
      imported = true;
    }

    if (ioctl(cap_ctx.fd, VIDIOC_QBUF, &buffer) != 0) {
      fatal("VIDIOC_QBUF (requeue) failed: " + std::string(std::strerror(errno)));
    }
  }
}

std::string default_device() {
  for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
    if (entry.is_character_file()) {
      const auto name = entry.path().filename().string();
      if (name.rfind("video", 0) == 0) {
        return entry.path();
      }
    }
  }
  fatal("No /dev/video* device nodes found");
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  std::string device_path = (argc > 1) ? argv[1] : default_device();

  syzygy::profiling::ScopeTimer timer{"DMA-BUF Vulkan latency prototype"};

  CaptureContext capture{};
  capture.fd = open_device(device_path);
  query_caps(capture.fd);
  set_format(capture.fd);

  v4l2_requestbuffers requests{};
  requests.count = kBufferCount;
  requests.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  requests.memory = V4L2_MEMORY_MMAP;

  if (ioctl(capture.fd, VIDIOC_REQBUFS, &requests) != 0) {
    fatal("VIDIOC_REQBUFS failed: " + std::string(std::strerror(errno)));
  }

  capture.buffers.reserve(requests.count);
  for (uint32_t i = 0; i < requests.count; ++i) {
    capture.buffers.push_back(create_buffer(capture.fd, static_cast<int>(i)));
    queue_buffer(capture.fd, static_cast<int>(i));
  }

  start_streaming(capture);

  VulkanContext vk = create_vulkan_context();
  capture_frames(capture, vk, 60);

  destroy_vulkan_context(vk);
  cleanup_capture(capture);

  syzygy::log::info("Phase 0 video prototype complete.");
  return 0;
}
