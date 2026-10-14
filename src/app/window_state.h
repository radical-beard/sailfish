#pragma once
#include <filesystem>
#include <optional>

namespace sailfish::app {

// Where the window was, so the next launch opens it there again.
struct WindowState {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool maximized = false;
};

std::optional<WindowState> load_window_state(const std::filesystem::path& file);
// False if the state couldn't be written (it's only a convenience).
bool save_window_state(const std::filesystem::path& file, const WindowState& state) noexcept;

} // namespace sailfish::app
