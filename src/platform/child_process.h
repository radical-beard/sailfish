#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "io/byte_stream.h"

namespace sailfish::platform {

class SpawnError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class StderrMode { Inherit, Discard, File, MergeIntoStdout };

struct ProcessSpec {
  std::string program;  // searched on PATH unless it contains a path separator
  std::vector<std::string> args;  // argv[1..]
  std::vector<std::pair<std::string, std::string>> env;  // added to / overriding the inherited environment
  std::filesystem::path cwd;  // empty: inherit
  StderrMode stderr_mode = StderrMode::Inherit;
  std::filesystem::path stderr_file;  // used with StderrMode::File (appended)
};

// A running child whose stdin and stdout are pipes owned by this object. It
// inherits no other file descriptors. Destroying it closes the pipes and
// reaps the child, killing it if it has not exited on its own.
class ChildProcess final : public io::ByteStream {
public:
  static std::unique_ptr<ChildProcess> spawn(const ProcessSpec& spec);
  ~ChildProcess() override;

  size_t read(std::span<char> buffer) override;
  void write(std::string_view bytes) override;
  void close_write() override;

  // Asks the child to exit (SIGTERM). On Windows the same as kill().
  void terminate();
  // Ends the child unconditionally (SIGKILL, TerminateProcess).
  void kill();
  // Blocks until the child exits. Exit code, or -signal on POSIX.
  int wait();
  [[nodiscard]] std::optional<int> try_wait();
  // Waits at most `timeout` for the child to exit; true if it has.
  bool wait_for(std::chrono::milliseconds timeout);

  struct Impl;

private:
  explicit ChildProcess(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

struct CapturedRun {
  int exit_code = 0;
  std::string output;
};

// Runs a child to completion, feeding `input` and collecting stdout.
CapturedRun run_captured(ProcessSpec spec, std::string_view input = {});

// Absolute path of `name` on PATH, if present.
std::optional<std::filesystem::path> find_executable(const std::string& name);

// Process-wide settings that child-process I/O relies on (POSIX: writing to
// a child that has exited fails with EPIPE instead of killing us). Call once,
// first thing in main.
void install_process_defaults();

} // namespace sailfish::platform
