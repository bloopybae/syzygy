cmake_minimum_required(VERSION 3.25)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE IMPORTED_TARGET pipewire-0.3)

if(NOT PIPEWIRE_FOUND)
  pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
endif()

if(NOT PIPEWIRE_FOUND)
  pkg_check_modules(PIPEWIRE IMPORTED_TARGET pipewire-1.0)
endif()

if(PIPEWIRE_FOUND)
  add_executable(audio_probe
    audio_probe.cpp
  )

  target_link_libraries(audio_probe PRIVATE
    syzygy_common
    PkgConfig::PIPEWIRE
  )

  target_compile_options(audio_probe PRIVATE -Wall -Wextra -Wpedantic)

  install(TARGETS audio_probe RUNTIME DESTINATION bin)
else()
  message(WARNING "PipeWire development package not found (pipewire-0.3/libpipewire-0.3/pipewire-1.0). Install the appropriate PipeWire headers to build audio_probe.")
endif()
