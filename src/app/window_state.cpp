#include "app/window_state.h"

#include <format>
#include <fstream>
#include <map>
#include <string>

#include "platform/files.h"

namespace sailfish::app {

// A few `key=value` lines: readable, and nothing to parse but integers.
std::optional<WindowState> load_window_state(const std::filesystem::path& file) {
  std::ifstream in(file);
  if (!in) {
    return std::nullopt;
  }
  std::map<std::string, int> values;
  for (std::string line; std::getline(in, line);) {
    const auto eq = line.find('=');
    if (eq != std::string::npos) {
      try {
        values[line.substr(0, eq)] = std::stoi(line.substr(eq + 1));
      } catch (const std::exception&) {
        return std::nullopt;  // a damaged file just means default placement
      }
    }
  }
  if (!values.contains("width") || !values.contains("height") || values["width"] < 200 || values["height"] < 150) {
    return std::nullopt;
  }
  return WindowState{values["x"], values["y"], values["width"], values["height"], values["maximized"] != 0};
}

bool save_window_state(const std::filesystem::path& file, const WindowState& state) noexcept {
  return platform::replace_file(file, std::format("x={}\ny={}\nwidth={}\nheight={}\nmaximized={}\n", state.x, state.y, state.width,
                                        state.height, state.maximized ? 1 : 0));
}

} // namespace sailfish::app
