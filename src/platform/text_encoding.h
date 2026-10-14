#pragma once
#include <string>
#include <string_view>

namespace sailfish::platform {

// UTF-8 <-> UTF-16 for Win32 wide APIs. Everything else in Sailfish is UTF-8.
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view utf16);

} // namespace sailfish::platform
