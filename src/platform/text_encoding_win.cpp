#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "platform/text_encoding.h"

namespace sailfish::platform {

std::wstring widen(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}

std::string narrow(std::wstring_view utf16) {
  if (utf16.empty()) {
    return {};
  }
  const int n = ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), out.data(), n, nullptr, nullptr);
  return out;
}

} // namespace sailfish::platform
