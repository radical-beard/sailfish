#include "services/task_runner.h"

namespace sailfish::services {

TaskRunner::TaskRunner(std::function<void()> wake) : wake_(std::move(wake)), worker_([this] { work_loop(); }) {}

TaskRunner::~TaskRunner() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_one();
  worker_.join();
}

void TaskRunner::post(std::function<void()> job) {
  {
    std::lock_guard lock(mutex_);
    jobs_.push_back(std::move(job));
  }
  cv_.notify_one();
}

void TaskRunner::complete(std::function<void()> completion) {
  {
    std::lock_guard lock(mutex_);
    completions_.push_back(std::move(completion));
  }
  wake_();
}

void TaskRunner::drain() {
  std::deque<std::function<void()>> ready;
  {
    std::lock_guard lock(mutex_);
    ready.swap(completions_);
  }
  for (auto& completion : ready) {
    completion();
  }
}

void TaskRunner::work_loop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (jobs_.empty()) {
        return;
      }
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();
  }
}

} // namespace sailfish::services
