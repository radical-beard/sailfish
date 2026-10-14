#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sailfish::services {

struct ConfigVersion {
  std::string hash;
  int64_t time = 0;  // unix seconds
  std::string message;
  std::string status;  // "good", "bad", or "" (never started)
};

// Every version of the user's Neovim config, so a bad edit can always be
// rolled back to the last config that started cleanly. All operations block;
// run them on a TaskRunner.
class ConfigHistory {
public:
  struct Recorded {
    std::optional<ConfigVersion> version;  // the newest version after recording
    // Whether the config may have changed: recording created a version, or
    // couldn't tell (history failed), in which case a reload must still happen.
    bool changed = false;
  };

  virtual ~ConfigHistory() = default;
  // Records the config's current content as a new version if it changed.
  virtual Recorded record(std::string_view message) = 0;
  virtual void mark(const std::string& hash, bool good) = 0;
  virtual std::optional<ConfigVersion> last_good() = 0;
  virtual std::vector<ConfigVersion> list(int limit) = 0;
  // A version's diff against its parent, for review.
  virtual std::string show(const std::string& hash) = 0;
  // Checks a version out under Sailfish's data dir. Returns the directory to
  // start Neovim with as XDG_CONFIG_HOME.
  virtual std::optional<std::filesystem::path> materialize(const std::string& hash) = 0;
  // Overwrites the real config with a version (recording the current state first).
  virtual bool restore(const std::string& hash) = 0;
};

// A version's hash as history reports it (full or abbreviated). Hashes that
// arrive from outside are checked with this before they reach git.
bool is_version_hash(std::string_view s);

// History in a private git repository (`--git-dir` under Sailfish's data dir,
// the config as work tree), so the user's own repository is never touched.
// Without git on PATH, history is unavailable and every query comes back empty.
std::shared_ptr<ConfigHistory> make_config_history(const std::filesystem::path& config_dir, const std::filesystem::path& data_dir,
  const std::string& app_name);

} // namespace sailfish::services
