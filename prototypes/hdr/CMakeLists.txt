cmake_minimum_required(VERSION 3.25)

add_executable(hdr_probe
  hdr_probe.cpp
)

target_link_libraries(hdr_probe PRIVATE syzygy_common)
target_compile_options(hdr_probe PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS hdr_probe RUNTIME DESTINATION bin)

