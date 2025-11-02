#include "util/thread_pool.hpp"

#include "syzygy/log.hpp"

namespace syzygy::util {

ThreadPool::ThreadPool(std::size_t thread_count) {
  if (thread_count == 0) {
    thread_count = 2;
  }
  threads_.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) {
    threads_.emplace_back([this]() { worker_loop(); });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

void ThreadPool::worker_loop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
      if (stopping_ && tasks_.empty()) {
        return;
      }
      task = std::move(tasks_.front());
      tasks_.pop();
    }
    try {
      task();
    } catch (const std::exception& ex) {
      syzygy::log::warn("ThreadPool task raised exception:", ex.what());
    } catch (...) {
      syzygy::log::warn("ThreadPool task raised unknown exception");
    }
  }
}

}  // namespace syzygy::util

