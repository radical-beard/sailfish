#pragma once
#include <filesystem>

namespace sailfish::platform {

// All Sailfish data lives here, on every OS: ~/.local/share/sailfish.
std::filesystem::path data_dir();

std::filesystem::path executable_path();

// Bundled resources: the Lua runtime (runtime/) and fonts (fonts/). Inside
// the app bundle or install prefix when installed, the staged copy in the
// build tree when running from a build directory.
std::filesystem::path resource_dir();

} // namespace sailfish::platform
