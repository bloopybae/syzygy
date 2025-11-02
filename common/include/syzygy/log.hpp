#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>
//
// Simple logging helpers used across the codebase.

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>

namespace syzygy::log {

namespace detail {
inline std::mutex& stream_mutex() {
  static std::mutex mutex;
  return mutex;
}

inline std::string timestamp() {
  using std::chrono::system_clock;
  const auto now = system_clock::now();
  const auto time = system_clock::to_time_t(now);
  const auto local = *std::localtime(&time);

  std::ostringstream oss;
  oss << std::put_time(&local, "%H:%M:%S");
  return oss.str();
}
}  // namespace detail

template <typename... Args>
void info(std::string_view message, Args&&... args) {
  std::lock_guard<std::mutex> lock(detail::stream_mutex());
  std::cout << "[INFO " << detail::timestamp() << "] " << message;
  ((std::cout << ' ' << std::forward<Args>(args)), ...);
  std::cout << std::endl;
}

template <typename... Args>
void warn(std::string_view message, Args&&... args) {
  std::lock_guard<std::mutex> lock(detail::stream_mutex());
  std::cerr << "[WARN " << detail::timestamp() << "] " << message;
  ((std::cerr << ' ' << std::forward<Args>(args)), ...);
  std::cerr << std::endl;
}

[[noreturn]] inline void fatal(std::string_view message) {
  std::lock_guard<std::mutex> lock(detail::stream_mutex());
  std::cerr << "[FATAL " << detail::timestamp() << "] " << message << std::endl;
  std::terminate();
}

}  // namespace syzygy::log

