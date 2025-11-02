cmake_minimum_required(VERSION 3.25)

find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV IMPORTED_TARGET libudev)

add_executable(video_probe
  video_probe.cpp
)

target_link_libraries(video_probe PRIVATE syzygy_common)
target_compile_definitions(video_probe PRIVATE _FILE_OFFSET_BITS=64)
target_compile_options(video_probe PRIVATE -Wall -Wextra -Wpedantic)

if(UDEV_FOUND)
  target_link_libraries(video_probe PRIVATE PkgConfig::UDEV)
  target_compile_definitions(video_probe PRIVATE SYZYGY_HAS_UDEV=1)
endif()

find_package(Vulkan)

if(Vulkan_FOUND)
  add_executable(video_dma_latency
    video_dma_latency.cpp
  )

  target_link_libraries(video_dma_latency PRIVATE
    syzygy_common
    Vulkan::Vulkan
  )

  target_compile_definitions(video_dma_latency PRIVATE _FILE_OFFSET_BITS=64)
  target_compile_options(video_dma_latency PRIVATE -Wall -Wextra -Wpedantic)

  if(UDEV_FOUND)
    target_link_libraries(video_dma_latency PRIVATE PkgConfig::UDEV)
    target_compile_definitions(video_dma_latency PRIVATE SYZYGY_HAS_UDEV=1)
  endif()
else()
  message(WARNING "Vulkan headers/libs not found. Install vulkan-headers/vulkan-loader to build video_dma_latency prototype.")
endif()

install(TARGETS video_probe
  RUNTIME DESTINATION bin
)

if(TARGET video_dma_latency)
  install(TARGETS video_dma_latency RUNTIME DESTINATION bin)
endif()
