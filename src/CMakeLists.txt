cmake_minimum_required(VERSION 3.25)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(UDEV REQUIRED libudev)
pkg_check_modules(V4L2 REQUIRED libv4l2)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)

set(SYZYGY_SRC
  app/application.cpp
  app/main_window.cpp
  app/video_widget.cpp
  audio/pipewire_controller.cpp
  capture/capture_device.cpp
  capture/capture_session.cpp
  capture/device_monitor.cpp
  settings/settings_manager.cpp
  util/thread_pool.cpp
)

add_library(syzygy_core STATIC
  ${SYZYGY_SRC}
)

target_include_directories(syzygy_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GTKMM_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${UDEV_INCLUDE_DIRS}
    ${V4L2_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
)

target_compile_options(syzygy_core
  PRIVATE
    -Wall -Wextra -Wpedantic
)

if(PIPEWIRE_FOUND)
  target_compile_definitions(syzygy_core PRIVATE SYZYGY_HAVE_PIPEWIRE=1)
endif()
if(UDEV_FOUND)
  target_compile_definitions(syzygy_core PRIVATE SYZYGY_HAVE_UDEV=1)
endif()

target_link_libraries(syzygy_core
  PUBLIC
    syzygy_common
    ${GTKMM_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${UDEV_LIBRARIES}
    ${V4L2_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
)

add_executable(syzygy_app app/main.cpp)

target_link_libraries(syzygy_app PRIVATE syzygy_core)

install(TARGETS syzygy_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/resources/syzygy.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES ${PROJECT_SOURCE_DIR}/resources/icon.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
        RENAME syzygy.png)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/syzygy)
