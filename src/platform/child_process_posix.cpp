#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <map>
#include <mutex>

#include "platform/child_process.h"

extern char** environ;

namespace sailfish::platform {

namespace {

void set_cloexec(int fd) { ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC); }

struct Pipe {
  int read_end = -1;
  int write_end = -1;
};

Pipe make_pipe() {
  int fds[2];
  if (::pipe(fds) != 0) {
    throw SpawnError(std::format("pipe() failed: {}", std::strerror(errno)));
  }
  set_cloexec(fds[0]);
  set_cloexec(fds[1]);
  return {fds[0], fds[1]};
}

void close_fd(int& fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

std::vector<std::string> merged_environment(const ProcessSpec& spec) {
  std::map<std::string, std::string> vars;
  for (char** e = environ; *e; ++e) {
    std::string_view entry(*e);
    const auto eq = entry.find('=');
    if (eq != std::string_view::npos) {
      vars[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
    }
  }
  for (const auto& [k, v] : spec.env) {
    vars[k] = v;
  }
  std::vector<std::string> out;
  out.reserve(vars.size());
  for (const auto& [k, v] : vars) {
    out.push_back(k + "=" + v);
  }
  return out;
}

std::vector<char*> c_strings(std::vector<std::string>& strings) {
  std::vector<char*> out;
  out.reserve(strings.size() + 1);
  for (auto& s : strings) {
    out.push_back(s.data());
  }
  out.push_back(nullptr);
  return out;
}

int decode_status(int status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return -WTERMSIG(status);
  }
  return -1;
}

} // namespace

struct ChildProcess::Impl {
  pid_t pid = -1;
  int stdin_fd = -1;
  int stdout_fd = -1;
  std::mutex write_mutex;
  std::optional<int> exit_code;
};

ChildProcess::ChildProcess(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

void install_process_defaults() { ::signal(SIGPIPE, SIG_IGN); }

std::unique_ptr<ChildProcess> ChildProcess::spawn(const ProcessSpec& spec) {
  Pipe in = make_pipe();
  Pipe out = make_pipe();

  // The child gets its three standard descriptors and nothing else. Without
  // this, a child spawned concurrently on another thread (nvim on the UI
  // thread, git on the worker) could inherit the other's pipe ends, and
  // that pipe would never report EOF.
  posix_spawnattr_t attrs;
  posix_spawnattr_init(&attrs);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
#if defined(__APPLE__)
  posix_spawnattr_setflags(&attrs, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
  posix_spawn_file_actions_adddup2(&actions, in.read_end, STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, out.write_end, STDOUT_FILENO);
  switch (spec.stderr_mode) {
    case StderrMode::Inherit:
#if defined(__APPLE__)
      posix_spawn_file_actions_addinherit_np(&actions, STDERR_FILENO);
#endif
      break;
    case StderrMode::Discard:
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
      break;
    case StderrMode::File:
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, spec.stderr_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
      break;
    case StderrMode::MergeIntoStdout:
      posix_spawn_file_actions_adddup2(&actions, out.write_end, STDERR_FILENO);
      break;
  }
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
  posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
#endif
#endif
  if (!spec.cwd.empty()) {
    // The _np spelling exists on every macOS and glibc we target; its POSIX
    // 2024 replacement only on macOS 26+ and recent glibc.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    posix_spawn_file_actions_addchdir_np(&actions, spec.cwd.c_str());
#pragma GCC diagnostic pop
  }

  std::vector<std::string> argv_strings;
  argv_strings.push_back(spec.program);
  argv_strings.insert(argv_strings.end(), spec.args.begin(), spec.args.end());
  auto argv = c_strings(argv_strings);
  auto env_strings = merged_environment(spec);
  auto envp = c_strings(env_strings);

  pid_t pid = -1;
  const int rc = ::posix_spawnp(&pid, spec.program.c_str(), &actions, &attrs, argv.data(), envp.data());
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attrs);
  close_fd(in.read_end);
  close_fd(out.write_end);
  if (rc != 0) {
    close_fd(in.write_end);
    close_fd(out.read_end);
    throw SpawnError(std::format("cannot start '{}': {}", spec.program, std::strerror(rc)));
  }

  auto impl = std::make_unique<Impl>();
  impl->pid = pid;
  impl->stdin_fd = in.write_end;
  impl->stdout_fd = out.read_end;
  return std::unique_ptr<ChildProcess>(new ChildProcess(std::move(impl)));
}

ChildProcess::~ChildProcess() {
  close_write();
  close_fd(impl_->stdout_fd);
  if (!try_wait()) {
    kill();
    wait();
  }
}

size_t ChildProcess::read(std::span<char> buffer) {
  for (;;) {
    const ssize_t n = ::read(impl_->stdout_fd, buffer.data(), buffer.size());
    if (n >= 0) {
      return static_cast<size_t>(n);
    }
    if (errno != EINTR) {
      return 0;
    }
  }
}

void ChildProcess::write(std::string_view bytes) {
  std::lock_guard lock(impl_->write_mutex);
  while (!bytes.empty()) {
    if (impl_->stdin_fd < 0) {
      throw io::StreamClosed("child stdin is closed");
    }
    const ssize_t n = ::write(impl_->stdin_fd, bytes.data(), bytes.size());
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw io::StreamClosed(std::format("write to child failed: {}", std::strerror(errno)));
    }
    bytes.remove_prefix(static_cast<size_t>(n));
  }
}

void ChildProcess::close_write() {
  std::lock_guard lock(impl_->write_mutex);
  close_fd(impl_->stdin_fd);
}

void ChildProcess::terminate() {
  if (!impl_->exit_code) {
    ::kill(impl_->pid, SIGTERM);
  }
}

void ChildProcess::kill() {
  if (!impl_->exit_code) {
    ::kill(impl_->pid, SIGKILL);
  }
}

int ChildProcess::wait() {
  if (!impl_->exit_code) {
    int status = 0;
    while (::waitpid(impl_->pid, &status, 0) < 0 && errno == EINTR) {
    }
    impl_->exit_code = decode_status(status);
  }
  return *impl_->exit_code;
}

std::optional<int> ChildProcess::try_wait() {
  if (!impl_->exit_code) {
    int status = 0;
    if (::waitpid(impl_->pid, &status, WNOHANG) == impl_->pid) {
      impl_->exit_code = decode_status(status);
    }
  }
  return impl_->exit_code;
}

std::optional<std::filesystem::path> find_executable(const std::string& name) {
  if (name.find('/') != std::string::npos) {
    return ::access(name.c_str(), X_OK) == 0 ? std::optional(std::filesystem::path(name)) : std::nullopt;
  }
  const char* path = std::getenv("PATH");
  std::string_view dirs = path ? path : "";
  while (!dirs.empty()) {
    const auto sep = dirs.find(':');
    const auto dir = dirs.substr(0, sep);
    if (!dir.empty()) {
      auto candidate = std::filesystem::path(dir) / name;
      if (::access(candidate.c_str(), X_OK) == 0) {
        return candidate;
      }
    }
    dirs = sep == std::string_view::npos ? std::string_view{} : dirs.substr(sep + 1);
  }
  return std::nullopt;
}

} // namespace sailfish::platform
