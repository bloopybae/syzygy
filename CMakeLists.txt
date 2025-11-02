cmake_minimum_required(VERSION 3.25)

project(syzygy VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SYZYGY_BUILD_PROTOTYPES "Build Phase 0 prototypes" ON)
option(SYZYGY_BUILD_APPIMAGE "Enable helper targets for AppImage packaging" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_subdirectory(common)
add_subdirectory(src)

if(SYZYGY_BUILD_PROTOTYPES)
  add_subdirectory(prototypes)
endif()

if(SYZYGY_BUILD_APPIMAGE)
  set(SYZYGY_APPDIR "${CMAKE_BINARY_DIR}/AppDir")
  add_custom_target(appdir
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${SYZYGY_APPDIR} --config $<CONFIG>
    DEPENDS syzygy_app
    COMMENT "Staging AppDir at ${SYZYGY_APPDIR}")

  set(SYZYGY_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
  set(SYZYGY_APPIMAGE "${SYZYGY_DIST_DIR}/Syzygy-${PROJECT_VERSION}.AppImage")
  set(APPIMAGETOOL_BIN $ENV{APPIMAGETOOL})
  if(APPIMAGETOOL_BIN)
    set(SYZYGY_APPIMAGE_COMMAND ${APPIMAGETOOL_BIN} ${SYZYGY_APPDIR} ${SYZYGY_APPIMAGE})
  else()
    message(STATUS "APPIMAGETOOL environment variable not set; appimage target will only stage AppDir.")
    set(SYZYGY_APPIMAGE_COMMAND ${CMAKE_COMMAND} -E echo "Set APPIMAGETOOL to the appimagetool binary to build the AppImage.")
  endif()

  add_custom_target(appimage
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SYZYGY_DIST_DIR}
    COMMAND ${SYZYGY_APPIMAGE_COMMAND}
    DEPENDS appdir
    COMMENT "Building AppImage at ${SYZYGY_APPIMAGE}")
endif()
