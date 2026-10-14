#include <thread>

#include "platform/child_process.h"

namespace sailfish::platform {

bool ChildProcess::wait_for(std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!try_wait()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

CapturedRun run_captured(ProcessSpec spec, std::string_view input) {
  auto child = ChildProcess::spawn(spec);
  // Feed stdin from a second thread so a child that writes a lot before
  // reading everything can't deadlock against us.
  std::thread feeder([&] {
    try {
      child->write(input);
    } catch (const io::StreamClosed&) {
      // The child exited without consuming all input; its exit code reports why.
    }
    child->close_write();
  });

  CapturedRun run;
  char buffer[16 * 1024];
  while (const size_t n = child->read(buffer)) {
    run.output.append(buffer, n);
  }
  feeder.join();
  run.exit_code = child->wait();
  return run;
}

} // namespace sailfish::platform
