#include "platform/paths.h"

#include <cstdlib>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace sailfish::platform {

namespace {

std::filesystem::path home_dir() {
#if defined(_WIN32)
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  if (!home || !*home) {
    throw std::runtime_error("cannot determine the home directory");
  }
  return home;
}

} // namespace

std::filesystem::path data_dir() { return home_dir() / ".local" / "share" / "sailfish"; }

std::filesystem::path executable_path() {
#if defined(_WIN32)
  wchar_t buffer[MAX_PATH * 4];
  const DWORD n = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
  return std::filesystem::path(std::wstring(buffer, n));
#elif defined(__APPLE__)
  char buffer[4096];
  uint32_t size = sizeof(buffer);
  _NSGetExecutablePath(buffer, &size);
  return std::filesystem::canonical(buffer);
#else
  return std::filesystem::canonical("/proc/self/exe");
#endif
}

std::filesystem::path resource_dir() {
  const auto exe_dir = executable_path().parent_path();
  const std::filesystem::path candidates[] = {
    exe_dir / ".." / "Resources",                     // macOS .app bundle
    exe_dir / ".." / "share" / "sailfish",            // Linux install prefix
    exe_dir,                                          // Windows, portable installs
    std::filesystem::path(SAILFISH_STAGED_RESOURCE_DIR),  // running from a build tree
  };
  for (const auto& dir : candidates) {
    if (std::filesystem::exists(dir / "runtime" / "lua" / "sailfish")) {
      return std::filesystem::weakly_canonical(dir);
    }
  }
  throw std::runtime_error("Sailfish's bundled resources are missing");
}

} // namespace sailfish::platform
