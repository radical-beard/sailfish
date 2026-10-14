#pragma once
#include <SDL3/SDL.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/keyboard.h"
#include "app/supervisor.h"
#include "host/clipboard_service.h"
#include "host/host_api.h"
#include "platform/font_locator.h"
#include "render/compositor.h"

namespace sailfish::app {

struct AppOptions {
  std::vector<std::string> nvim_args;  // forwarded to nvim (files, +cmd, ...)
};

// The composition root: creates the window, renderer, fonts, host services
// and the Neovim instance, and runs the event loop that ties them together.
class Application {
public:
  explicit Application(AppOptions options);
  ~Application();
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  int run();

private:
  void handle(const SDL_Event& e);
  void apply_ui_changes();
  void update_font();
  void fit_grid_to_window();
  void wake_from_any_thread();
  void register_host_methods();
  [[nodiscard]] SupervisorPorts supervisor_ports();
  void on_active_changed();
  void apply_title();
  void remember_window();
  // Milliseconds SDL may sleep before the next frame or supervisor deadline (-1: until an event).
  [[nodiscard]] Sint32 wait_timeout(std::optional<double> next_frame) const;

  AppOptions options_;
  // Declared first so they outlive everything that can wake the loop.
  SDL_EventType wake_event_ = SDL_EVENT_FIRST;
  std::atomic<bool> wake_pending_ = false;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  std::unique_ptr<platform::FontLocator> locator_;
  std::unique_ptr<render::Compositor> compositor_;
  host::HostApi host_api_;
  host::ClipboardService clipboard_;
  std::unique_ptr<services::TaskRunner> tasks_;
  std::unique_ptr<Supervisor> supervisor_;
  Keyboard keyboard_;

  bool focused_ = true;
  bool dirty_ = true;
  bool attached_ = false;
  std::string mode_label_;
  int cols_ = 0;
  int rows_ = 0;
};

} // namespace sailfish::app
