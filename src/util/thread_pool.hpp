#pragma once

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace syzygy::util {

class ThreadPool {
 public:
  explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency());
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <typename Func, typename... Args>
  auto enqueue(Func&& func, Args&&... args)
      -> std::future<std::invoke_result_t<Func, Args...>> {
    using ReturnType = std::invoke_result_t<Func, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

    std::future<ReturnType> result = task->get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      tasks_.emplace([task]() { (*task)(); });
    }
    cv_.notify_one();
    return result;
  }

 private:
  void worker_loop();

  std::vector<std::thread> threads_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_{false};
};

}  // namespace syzygy::util
