cmake_minimum_required(VERSION 3.25)

add_library(syzygy_common INTERFACE)
target_include_directories(syzygy_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(syzygy_common INTERFACE cxx_std_20)
target_compile_options(syzygy_common INTERFACE
  -Wall
  -Wextra
  -Wpedantic
)

