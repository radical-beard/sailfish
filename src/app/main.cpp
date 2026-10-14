#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "app/application.h"
#include "log/log.h"
#include "platform/child_process.h"
#include "platform/paths.h"

namespace {

constexpr std::uintmax_t kMaxLogBytes = 4 * 1024 * 1024;

// One previous generation is kept, so a log never grows without bound.
void rotate(const std::filesystem::path& log) {
  std::error_code ec;
  if (std::filesystem::file_size(log, ec) > kMaxLogBytes && !ec) {
    auto previous = log;
    previous += ".1";
    std::filesystem::rename(log, previous, ec);
  }
}

void install_log_file() {
  const auto dir = sailfish::platform::data_dir() / "logs";
  std::filesystem::create_directories(dir);
  rotate(dir / "sailfish.log");
  rotate(dir / "nvim-stderr.log");
  auto file = std::make_shared<std::ofstream>(dir / "sailfish.log", std::ios::app);
  sailfish::log::set_sink([file](sailfish::log::Level level, std::string_view message) {
    const std::string_view name = sailfish::log::level_name(level);
    *file << '[' << name << "] " << message << '\n';
    file->flush();
    if (level != sailfish::log::Level::Debug) {
      std::fprintf(stderr, "[%.*s] %.*s\n", static_cast<int>(name.size()), name.data(), static_cast<int>(message.size()), message.data());
    }
  });
}

} // namespace

int main(int argc, char** argv) {
  sailfish::platform::install_process_defaults();
  try {
    install_log_file();
    sailfish::app::AppOptions options;
    for (int i = 1; i < argc; ++i) {
      options.nvim_args.emplace_back(argv[i]);
    }
    sailfish::app::Application app(std::move(options));
    return app.run();
  } catch (const std::exception& e) {
    sailfish::log::error("fatal: {}", e.what());
    // Launched from the Dock or Finder there's no terminal to read that in.
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Sailfish could not continue", e.what(), nullptr);
    return 1;
  }
}
