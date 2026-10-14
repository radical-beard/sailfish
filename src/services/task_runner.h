#pragma once
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

namespace sailfish::services {

// Runs blocking work (git, file I/O) on a background thread and delivers
// each result back on the UI thread, in submission order. Work reports
// expected failures in its result; an exception it throws is a bug, and is
// rethrown on the UI thread by drain().
class TaskRunner {
public:
  explicit TaskRunner(std::function<void()> wake);
  ~TaskRunner();
  TaskRunner(const TaskRunner&) = delete;
  TaskRunner& operator=(const TaskRunner&) = delete;

  // `done` receives work's result, or nothing when work returns void.
  template <typename Work, typename Done>
  void run(Work work, Done done) {
    post([this, work = std::move(work), done = std::move(done)]() mutable {
      try {
        if constexpr (std::is_void_v<decltype(work())>) {
          work();
          complete(std::move(done));
        } else {
          auto result = std::make_shared<decltype(work())>(work());
          complete([done = std::move(done), result]() mutable { done(std::move(*result)); });
        }
      } catch (...) {
        complete([error = std::current_exception()] { std::rethrow_exception(error); });
      }
    });
  }

  template <typename Work>
  void run(Work work) {
    run(std::move(work), [](auto&&...) {});
  }

  // UI thread: runs finished tasks' completions.
  void drain();

private:
  void post(std::function<void()> job);
  void complete(std::function<void()> completion);
  void work_loop();

  std::function<void()> wake_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  std::deque<std::function<void()>> completions_;
  bool stopping_ = false;
  std::thread worker_;
};

} // namespace sailfish::services
