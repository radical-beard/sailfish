#pragma once
#include <format>
#include <functional>
#include <string_view>
#include <utility>

// Process-wide logging. The one deliberate global: threading a logger through
// every constructor would add noise to every unit for no testability gain.
// The composition root installs the sink; the default writes to stderr.
namespace sailfish::log {

enum class Level { Debug, Info, Warn, Error };

using Sink = std::function<void(Level, std::string_view)>;

// "debug", "info", "warn", "error".
std::string_view level_name(Level level);
void set_sink(Sink sink);
void write(Level level, std::string_view message);

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace sailfish::log
