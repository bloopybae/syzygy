cmake_minimum_required(VERSION 3.25)

project(syzygy_prototypes VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SYZYGY_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SYZYGY_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(SYZYGY_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address -fno-omit-frame-pointer)
endif()

if(SYZYGY_ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread -fno-omit-frame-pointer)
endif()

add_subdirectory(video)
add_subdirectory(audio)
add_subdirectory(hdr)

message(STATUS "Syzygy Phase 0 prototype workspace configured. Add targets as prototypes land.")
