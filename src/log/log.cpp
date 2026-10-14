#include "log/log.h"

#include <cstdio>
#include <mutex>

namespace sailfish::log {

namespace {

std::mutex& sink_mutex() {
  static std::mutex m;
  return m;
}

Sink& sink() {
  static Sink s = [](Level level, std::string_view message) {
    const std::string_view name = level_name(level);
    std::fprintf(stderr, "[%.*s] %.*s\n", static_cast<int>(name.size()), name.data(), static_cast<int>(message.size()), message.data());
  };
  return s;
}

} // namespace

std::string_view level_name(Level level) {
  switch (level) {
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
  }
  return "?";
}

void set_sink(Sink new_sink) {
  std::lock_guard lock(sink_mutex());
  sink() = std::move(new_sink);
}

void write(Level level, std::string_view message) {
  std::lock_guard lock(sink_mutex());
  sink()(level, message);
}

} // namespace sailfish::log
