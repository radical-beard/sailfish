#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <mutex>

#include "platform/child_process.h"
#include "platform/text_encoding.h"

namespace sailfish::platform {

namespace {

struct Handle {
  HANDLE h = nullptr;
  Handle() = default;
  explicit Handle(HANDLE handle) : h(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& o) noexcept : h(std::exchange(o.h, nullptr)) {}
  Handle& operator=(Handle&& o) noexcept {
    reset();
    h = std::exchange(o.h, nullptr);
    return *this;
  }
  ~Handle() { reset(); }
  void reset() {
    if (h && h != INVALID_HANDLE_VALUE) {
      ::CloseHandle(h);
    }
    h = nullptr;
  }
};

// "" when the variable is unset.
std::wstring environment_variable(const wchar_t* name) {
  const DWORD size = ::GetEnvironmentVariableW(name, nullptr, 0);
  if (size == 0) {
    return {};
  }
  std::wstring value(size, L'\0');
  value.resize(::GetEnvironmentVariableW(name, value.data(), size));
  return value;
}

[[noreturn]] void fail(std::string_view what) {
  throw SpawnError(std::format("{} failed (Win32 error {})", what, ::GetLastError()));
}

// Quotes one argument so CommandLineToArgvW (and the MSVC CRT) reproduce it.
void append_quoted(std::wstring& cmd, const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    cmd += arg;
    return;
  }
  cmd += L'"';
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      cmd.append(backslashes * 2 + 1, L'\\');
      cmd += L'"';
      backslashes = 0;
    } else {
      cmd.append(backslashes, L'\\');
      cmd += c;
      backslashes = 0;
    }
  }
  cmd.append(backslashes * 2, L'\\');
  cmd += L'"';
}

// CreateProcess wants a sorted, double-NUL-terminated UTF-16 block.
std::wstring environment_block(const ProcessSpec& spec) {
  std::vector<std::pair<std::wstring, std::wstring>> vars;
  if (wchar_t* block = ::GetEnvironmentStringsW()) {
    for (const wchar_t* p = block; *p; p += std::wcslen(p) + 1) {
      std::wstring_view entry(p);
      const auto eq = entry.find(L'=', 1);  // entries like "=C:=C:\" start with '='
      if (eq != std::wstring_view::npos) {
        vars.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
      }
    }
    ::FreeEnvironmentStringsW(block);
  }
  const auto same_name = [](const std::wstring& a, const std::wstring& b) { return ::_wcsicmp(a.c_str(), b.c_str()) == 0; };
  for (const auto& [k, v] : spec.env) {
    const std::wstring key = widen(k);
    auto it = std::find_if(vars.begin(), vars.end(), [&](const auto& kv) { return same_name(kv.first, key); });
    if (it != vars.end()) {
      it->second = widen(v);
    } else {
      vars.emplace_back(key, widen(v));
    }
  }
  std::sort(vars.begin(), vars.end(), [](const auto& a, const auto& b) { return ::_wcsicmp(a.first.c_str(), b.first.c_str()) < 0; });
  std::wstring block;
  for (const auto& [k, v] : vars) {
    block += k + L"=" + v;
    block += L'\0';
  }
  block += L'\0';
  return block;
}

} // namespace

struct ChildProcess::Impl {
  Handle process;
  Handle stdin_write;
  Handle stdout_read;
  std::mutex write_mutex;
  std::optional<int> exit_code;
};

ChildProcess::ChildProcess(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<ChildProcess> ChildProcess::spawn(const ProcessSpec& spec) {
  SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

  Handle stdin_read, stdin_write, stdout_read, stdout_write;
  if (!::CreatePipe(&stdin_read.h, &stdin_write.h, &inheritable, 0) || !::CreatePipe(&stdout_read.h, &stdout_write.h, &inheritable, 0)) {
    fail("CreatePipe");
  }
  ::SetHandleInformation(stdin_write.h, HANDLE_FLAG_INHERIT, 0);
  ::SetHandleInformation(stdout_read.h, HANDLE_FLAG_INHERIT, 0);

  Handle stderr_handle;
  HANDLE child_stderr = nullptr;
  switch (spec.stderr_mode) {
    case StderrMode::Inherit:
    case StderrMode::Discard:
      // A GUI-subsystem process has no console; give the child NUL rather than an invalid handle.
      stderr_handle = Handle(::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr));
      child_stderr = stderr_handle.h;
      break;
    case StderrMode::File:
      stderr_handle = Handle(::CreateFileW(spec.stderr_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_ALWAYS, 0, nullptr));
      child_stderr = stderr_handle.h;
      break;
    case StderrMode::MergeIntoStdout:
      child_stderr = stdout_write.h;
      break;
  }

  // Pass exactly these handles: concurrent spawns must not inherit each
  // other's pipe ends, or EOF never arrives.
  HANDLE inherit_list[3] = {stdin_read.h, stdout_write.h, child_stderr};
  const DWORD inherit_count = child_stderr == stdout_write.h ? 2 : 3;
  SIZE_T attr_size = 0;
  ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
  std::vector<std::byte> attr_storage(attr_size);
  auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_storage.data());
  if (!::InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) ||
      !::UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit_list, inherit_count * sizeof(HANDLE), nullptr, nullptr)) {
    fail("UpdateProcThreadAttribute");
  }

  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = stdin_read.h;
  si.StartupInfo.hStdOutput = stdout_write.h;
  si.StartupInfo.hStdError = child_stderr;
  si.lpAttributeList = attrs;

  const auto program = find_executable(spec.program);
  if (!program) {
    throw SpawnError(std::format("cannot find '{}' on PATH", spec.program));
  }
  std::wstring cmd;
  append_quoted(cmd, program->wstring());
  for (const auto& arg : spec.args) {
    cmd += L' ';
    append_quoted(cmd, widen(arg));
  }
  std::wstring env = environment_block(spec);
  const std::wstring cwd = spec.cwd.wstring();

  PROCESS_INFORMATION pi{};
  const BOOL ok = ::CreateProcessW(program->c_str(), cmd.data(), nullptr, nullptr, TRUE,
    CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, env.data(),
    cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
  ::DeleteProcThreadAttributeList(attrs);
  if (!ok) {
    fail(std::format("CreateProcess('{}')", spec.program));
  }
  ::CloseHandle(pi.hThread);

  auto impl = std::make_unique<Impl>();
  impl->process = Handle(pi.hProcess);
  impl->stdin_write = std::move(stdin_write);
  impl->stdout_read = std::move(stdout_read);
  return std::unique_ptr<ChildProcess>(new ChildProcess(std::move(impl)));
}

ChildProcess::~ChildProcess() {
  close_write();
  impl_->stdout_read.reset();
  if (!try_wait()) {
    kill();
    wait();
  }
}

size_t ChildProcess::read(std::span<char> buffer) {
  DWORD n = 0;
  if (!::ReadFile(impl_->stdout_read.h, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr)) {
    return 0;  // ERROR_BROKEN_PIPE: the child closed its stdout
  }
  return n;
}

void ChildProcess::write(std::string_view bytes) {
  std::lock_guard lock(impl_->write_mutex);
  while (!bytes.empty()) {
    if (!impl_->stdin_write.h) {
      throw io::StreamClosed("child stdin is closed");
    }
    DWORD n = 0;
    if (!::WriteFile(impl_->stdin_write.h, bytes.data(), static_cast<DWORD>(bytes.size()), &n, nullptr)) {
      throw io::StreamClosed(std::format("write to child failed (Win32 error {})", ::GetLastError()));
    }
    bytes.remove_prefix(n);
  }
}

void ChildProcess::close_write() {
  std::lock_guard lock(impl_->write_mutex);
  impl_->stdin_write.reset();
}

void ChildProcess::terminate() { kill(); }

void ChildProcess::kill() {
  if (!impl_->exit_code) {
    ::TerminateProcess(impl_->process.h, 1);
  }
}

void install_process_defaults() {}

int ChildProcess::wait() {
  if (!impl_->exit_code) {
    ::WaitForSingleObject(impl_->process.h, INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(impl_->process.h, &code);
    impl_->exit_code = static_cast<int>(code);
  }
  return *impl_->exit_code;
}

std::optional<int> ChildProcess::try_wait() {
  if (!impl_->exit_code && ::WaitForSingleObject(impl_->process.h, 0) == WAIT_OBJECT_0) {
    DWORD code = 0;
    ::GetExitCodeProcess(impl_->process.h, &code);
    impl_->exit_code = static_cast<int>(code);
  }
  return impl_->exit_code;
}

std::optional<std::filesystem::path> find_executable(const std::string& name) {
  const auto is_file = [](const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
  };
  std::vector<std::wstring> extensions{L""};
  if (std::filesystem::path(name).extension().empty()) {
    const std::wstring pathext = environment_variable(L"PATHEXT");
    std::wstring_view exts = pathext.empty() ? L".COM;.EXE;.BAT;.CMD" : std::wstring_view(pathext);
    while (!exts.empty()) {
      const auto sep = exts.find(L';');
      extensions.emplace_back(exts.substr(0, sep));
      exts = sep == std::wstring_view::npos ? std::wstring_view{} : exts.substr(sep + 1);
    }
  }
  const std::wstring wname = widen(name);
  const auto try_dir = [&](const std::filesystem::path& dir) -> std::optional<std::filesystem::path> {
    for (const auto& ext : extensions) {
      auto candidate = dir / (wname + ext);
      if (is_file(candidate)) {
        return candidate;
      }
    }
    return std::nullopt;
  };
  if (name.find_first_of("/\\") != std::string::npos) {
    for (const auto& ext : extensions) {
      if (std::filesystem::path candidate(wname + ext); is_file(candidate)) {
        return candidate;
      }
    }
    return std::nullopt;
  }
  const std::wstring path = environment_variable(L"PATH");
  std::wstring_view dirs(path);
  while (!dirs.empty()) {
    const auto sep = dirs.find(L';');
    const auto dir = dirs.substr(0, sep);
    if (!dir.empty()) {
      if (auto found = try_dir(std::filesystem::path(dir))) {
        return found;
      }
    }
    dirs = sep == std::wstring_view::npos ? std::wstring_view{} : dirs.substr(sep + 1);
  }
  return std::nullopt;
}

} // namespace sailfish::platform
