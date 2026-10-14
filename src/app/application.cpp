#include "app/application.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <stdexcept>

#include "app/window_state.h"
#include "log/log.h"
#include "nvim/instance.h"
#include "platform/paths.h"
#include "render/cmdline_layer.h"
#include "render/cursor_layer.h"
#include "render/grid_layer.h"
#include "render/message_layer.h"
#include "render/popupmenu_layer.h"
#include "render/toast_layer.h"

namespace sailfish::app {

namespace {

constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 800;

// Points to device pixels at a display scale of 1: macOS draws 1pt as one
// logical pixel; Windows and Linux use 96 DPI.
#if defined(__APPLE__)
constexpr double kPixelsPerPoint = 1.0;
constexpr bool kAltIsMeta = false;  // Option types characters unless configured otherwise
#else
constexpr double kPixelsPerPoint = 96.0 / 72.0;
constexpr bool kAltIsMeta = true;
#endif

double seconds_now() { return static_cast<double>(SDL_GetTicksNS()) / 1e9; }

std::filesystem::path window_state_file() { return platform::data_dir() / "state" / "window"; }

// Neovim keeps its config in $XDG_CONFIG_HOME/$NVIM_APPNAME; history
// snapshots of it have to be laid out the same way.
std::string nvim_app_name() {
  const char* name = std::getenv("NVIM_APPNAME");
  return name && *name ? name : "nvim";
}

msgpack::Map attach_options() {
  return {
    {"rgb", true},
    {"ext_linegrid", true},
    {"ext_multigrid", true},
    {"ext_cmdline", true},
    {"ext_messages", true},
    {"ext_popupmenu", true},
  };
}

} // namespace

Application::Application(AppOptions options)
  : options_(std::move(options)), keyboard_([this](std::string keys) { supervisor_->input(keys); }, kAltIsMeta) {
  SDL_SetHint(SDL_HINT_APP_NAME, "Sailfish");
  SDL_SetHint(SDL_HINT_MAC_PRESS_AND_HOLD, "0");  // held keys repeat (hjkl), no accent menu
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
  }
  // Created hidden so the remembered geometry applies before it's seen.
  window_ = SDL_CreateWindow("Sailfish", kDefaultWidth, kDefaultHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  if (window_) {
    if (const auto saved = load_window_state(window_state_file())) {
      SDL_SetWindowSize(window_, saved->width, saved->height);
      SDL_SetWindowPosition(window_, saved->x, saved->y);
      if (saved->maximized) {
        SDL_MaximizeWindow(window_);
      }
    }
    SDL_ShowWindow(window_);
  }
  renderer_ = window_ ? SDL_CreateRenderer(window_, nullptr) : nullptr;
  if (!renderer_) {
    throw std::runtime_error(std::string("cannot create window: ") + SDL_GetError());
  }
  SDL_SetRenderVSync(renderer_, 1);
  SDL_HideCursor();  // keyboard-only by design
  SDL_StartTextInput(window_);
  log::info("renderer: {}", SDL_GetRendererName(renderer_));

  wake_event_ = static_cast<SDL_EventType>(SDL_RegisterEvents(1));
  const auto resources = platform::resource_dir();
  locator_ = platform::make_font_locator();
  const std::vector<platform::FontFile> bundled_fonts{{resources / "fonts" / "SymbolsNerdFontMono-Regular.ttf", ""}};
  compositor_ = std::make_unique<render::Compositor>(renderer_, *locator_, bundled_fonts,
    render::FontRequest{{}, render::kDefaultPointSize * kPixelsPerPoint * SDL_GetWindowDisplayScale(window_), 0});
  compositor_->add_layer(std::make_unique<render::GridLayer>());
  compositor_->add_layer(std::make_unique<render::MessageLayer>());
  compositor_->add_layer(std::make_unique<render::CmdlineLayer>());
  compositor_->add_layer(std::make_unique<render::PopupmenuLayer>());
  compositor_->add_layer(std::make_unique<render::ToastLayer>());
  compositor_->add_layer(std::make_unique<render::CursorLayer>());

  register_host_methods();

  nvim::LaunchOptions launch;
  launch.pre_config_commands = {"lua dofile(vim.g.sailfish.runtime .. '/lua/sailfish/boot.lua')"};
  launch.args = options_.nvim_args;
  launch.ui_options = attach_options();
  launch.globals = {
    {"version", "0.1.0"},
    {"runtime", (resources / "runtime").string()},
    {"data_dir", platform::data_dir().string()},
    {"default_font", std::format("{}:h{}", compositor_->font_family(), render::kDefaultPointSize)},
  };
  launch.stderr_log = platform::data_dir() / "logs" / "nvim-stderr.log";
  tasks_ = std::make_unique<services::TaskRunner>([this] { wake_from_any_thread(); });
  supervisor_ = std::make_unique<Supervisor>(launch, platform::data_dir(), supervisor_ports(), host_api_, *tasks_,
    SupervisorEvents{
      .active_changed = [this] { on_active_changed(); },
      .mode_changed =
        [this](const std::string& label) {
          mode_label_ = label;
          apply_title();
        },
      .message_ui_changed = [this] { fit_grid_to_window(); },
    });
}

SupervisorPorts Application::supervisor_ports() {
  return {
    .launch = [this](const nvim::LaunchOptions& options, int cols, int rows) -> std::unique_ptr<nvim::Editor> {
      return std::make_unique<nvim::Instance>(options, cols, rows, [this] { wake_from_any_thread(); }, host_api_);
    },
    .open_history = [](const std::filesystem::path& config_dir) {
      return services::make_config_history(config_dir, platform::data_dir(), nvim_app_name());
    },
    .clock = seconds_now,
  };
}

Application::~Application() {
  remember_window();
  supervisor_.reset();  // closes every Neovim
  tasks_.reset();       // finishes background work; nothing wakes the loop after this
  compositor_.reset();
  SDL_DestroyRenderer(renderer_);
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

void Application::register_host_methods() {
  clipboard_.register_methods(host_api_);
  host_api_.on_notification("sailfish.debug.screenshot", [this](nvim::Editor&, const msgpack::Array& params) {
    compositor_->request_screenshot(params.at(0).as_string());
    dirty_ = true;
  });
}

void Application::wake_from_any_thread() {
  if (!wake_pending_.exchange(true)) {
    SDL_Event e{};
    e.type = wake_event_;
    if (!SDL_PushEvent(&e)) {
      wake_pending_ = false;  // a later wake must try again
    }
  }
}

void Application::remember_window() {
  WindowState state;
  state.maximized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
  SDL_GetWindowPosition(window_, &state.x, &state.y);
  SDL_GetWindowSize(window_, &state.width, &state.height);
  if (!save_window_state(window_state_file(), state)) {
    log::warn("cannot remember the window position");
  }
}

void Application::on_active_changed() {
  // The new instance's guifont, title and grid arrived while it was hidden.
  compositor_->reset();
  update_font();
  fit_grid_to_window();
  supervisor_->resize(cols_, rows_);
  apply_title();
  dirty_ = true;
}

void Application::apply_title() {
  const std::string& title = supervisor_->active().ui().title;
  std::string full = title.empty() ? "Sailfish" : title;
  if (!mode_label_.empty()) {
    full += "  [" + mode_label_ + "]";
  }
  SDL_SetWindowTitle(window_, full.c_str());
}

void Application::update_font() {
  const ui::UiOptions& o = supervisor_->active().ui().options;
  const double scale = SDL_GetWindowDisplayScale(window_);
  render::FontSpec spec = render::parse_guifont(o.guifont);
  const double pixels = spec.size * kPixelsPerPoint * scale;
  compositor_->set_font({std::move(spec), pixels, static_cast<int>(std::lround(o.linespace * scale))});
}

void Application::fit_grid_to_window() {
  int w = 0;
  int h = 0;
  SDL_GetWindowSizeInPixels(window_, &w, &h);
  const render::CellMetrics& cell = compositor_->cell();
  const int cols = std::max(1, w / cell.width);
  // The command bar takes the last row when Sailfish draws messages itself.
  const int rows = std::max(1, h / cell.height - (supervisor_->native_messages() ? 1 : 0));
  if (cols != cols_ || rows != rows_) {
    cols_ = cols;
    rows_ = rows;
    if (attached_) {
      supervisor_->resize(cols, rows);
    }
  }
  dirty_ = true;
}

void Application::apply_ui_changes() {
  ui::UiState& ui = supervisor_->active().ui();
  if (ui.changes.font) {
    update_font();
    fit_grid_to_window();
  }
  if (ui.changes.title) {
    apply_title();
  }
  if (ui.changes.flushed) {
    dirty_ = true;
  }
  ui.changes = {};
}

void Application::handle(const SDL_Event& e) {
  if (e.type == wake_event_) {
    wake_pending_ = false;
    supervisor_->pump();
    tasks_->drain();
    return;
  }
  switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      supervisor_->request_close();
      break;
    case SDL_EVENT_KEY_DOWN:
      compositor_->key_pressed();
      keyboard_.key_down(e.key);
      dirty_ = true;
      break;
    case SDL_EVENT_KEY_UP:
      keyboard_.key_up();
      break;
    case SDL_EVENT_TEXT_INPUT:
      keyboard_.text_input(e.text);
      break;
    case SDL_EVENT_TEXT_EDITING:
      keyboard_.text_editing(e.edit);
      break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      fit_grid_to_window();
      break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      update_font();
      fit_grid_to_window();
      break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      focused_ = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
      supervisor_->set_focus(focused_);
      dirty_ = true;
      break;
    case SDL_EVENT_WINDOW_EXPOSED:
      dirty_ = true;
      break;
    default:
      break;
  }
}

Sint32 Application::wait_timeout(std::optional<double> next_frame) const {
  std::optional<double> wait = next_frame;
  if (const auto deadline = supervisor_->next_deadline()) {
    const double until = std::max(0.0, *deadline - seconds_now());
    wait = wait ? std::min(*wait, until) : until;
  }
  return wait ? static_cast<Sint32>(std::ceil(*wait * 1000.0)) : -1;
}

int Application::run() {
  fit_grid_to_window();
  supervisor_->start(cols_, rows_);
  attached_ = true;

  std::optional<double> next_frame;
  while (!supervisor_->finished()) {
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, wait_timeout(next_frame))) {
      handle(e);
      while (SDL_PollEvent(&e)) {
        handle(e);
      }
    }
    supervisor_->poll();
    if (supervisor_->finished()) {
      break;
    }
    apply_ui_changes();
    if (dirty_ || next_frame) {
      next_frame = compositor_->render(supervisor_->active().ui(), {focused_, supervisor_->native_messages(), seconds_now()});
      dirty_ = false;
    }
  }
  return 0;
}

} // namespace sailfish::app
