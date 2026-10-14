#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "platform/child_process.h"
#include "services/config_history.h"

using namespace sailfish;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path = fs::temp_directory_path() / ("sailfish-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempDir() { fs::create_directories(path); }
  ~TempDir() { fs::remove_all(path); }
};

void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << text;
}

std::string read(const fs::path& p) {
  std::stringstream s;
  s << std::ifstream(p).rdbuf();
  return s.str();
}

} // namespace

TEST_CASE("config history records, marks, materializes and restores versions") {
  if (!platform::find_executable("git")) {
    MESSAGE("git not on PATH; skipping");
    return;
  }
  TempDir tmp;
  const fs::path config = tmp.path / "config" / "nvim";
  write(config / "init.lua", "vim.o.number = true\n");
  write(config / "lua" / "me" / "keys.lua", "return {}\n");
  auto history = services::make_config_history(config, tmp.path / "data", "nvim");

  const auto first = history->record("first");
  REQUIRE(first.version);
  CHECK(first.changed);
  CHECK(first.version->message == "first");

  CHECK_FALSE(history->record("nothing changed").changed);

  history->mark(first.version->hash, true);
  write(config / "init.lua", "vim.o.number = false\n");
  const auto second = history->record("second");
  REQUIRE(second.version);
  CHECK(second.changed);
  history->mark(second.version->hash, false);

  REQUIRE(history->last_good());
  CHECK(history->last_good()->hash == first.version->hash);

  const auto versions = history->list(10);
  REQUIRE(versions.size() == 2);
  CHECK(versions[0].status == "bad");
  CHECK(versions[1].status == "good");
  CHECK(history->show(second.version->hash).find("number = false") != std::string::npos);

  const auto snapshot = history->materialize(first.version->hash);
  REQUIRE(snapshot);
  CHECK(read(*snapshot / "nvim" / "init.lua") == "vim.o.number = true\n");
  CHECK(read(*snapshot / "nvim" / "lua" / "me" / "keys.lua") == "return {}\n");

  CHECK(history->restore(first.version->hash));
  CHECK(read(config / "init.lua") == "vim.o.number = true\n");
  // The pre-restore state was already a version, so only "Restored" is new.
  const auto after = history->list(10);
  REQUIRE(after.size() == 3);
  CHECK(after[0].message.starts_with("Restored"));
}
