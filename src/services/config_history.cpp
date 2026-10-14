#include "services/config_history.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <utility>

#include "log/log.h"
#include "platform/child_process.h"

namespace sailfish::services {

namespace {

constexpr char kUnit = '\x1f';
constexpr char kRecord = '\x1e';
constexpr std::string_view kFormat = "--format=%H%x1f%ct%x1f%s%x1f%N%x1e";
constexpr size_t kKeptSnapshots = 5;

#if defined(_WIN32)
constexpr const char* kNullDevice = "NUL";
#else
constexpr const char* kNullDevice = "/dev/null";
#endif

std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t at = s.find(sep); at != std::string_view::npos; at = s.find(sep, start)) {
    parts.emplace_back(s.substr(start, at - start));
    start = at + 1;
  }
  parts.emplace_back(s.substr(start));
  return parts;
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
  while (!s.empty() && (s.front() == '\n' || s.front() == ' ')) s.erase(0, 1);
  return s;
}

std::optional<ConfigVersion> parse_version(std::string_view record) {
  const auto fields = split(trim(std::string(record)), kUnit);
  int64_t time = 0;
  if (fields.size() < 3 || fields[0].empty() ||
      std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), time).ec != std::errc{}) {
    return std::nullopt;
  }
  return ConfigVersion{fields[0], time, fields[2], fields.size() > 3 ? trim(fields[3]) : ""};
}

// Keeps the newest snapshots; older ones are only kept for a safe mode
// nobody is in anymore.
void prune_snapshots(const std::filesystem::path& dir) {
  std::error_code ec;
  std::vector<std::filesystem::directory_entry> entries;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    entries.push_back(entry);
  }
  if (entries.size() <= kKeptSnapshots) {
    return;
  }
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    std::error_code ignored;
    return a.last_write_time(ignored) > b.last_write_time(ignored);
  });
  for (size_t i = kKeptSnapshots; i < entries.size(); ++i) {
    std::filesystem::remove_all(entries[i].path(), ec);
  }
}

// Stable, readable repository name per config directory.
std::string repo_name(const std::filesystem::path& config_dir) {
  uint32_t hash = 2166136261u;
  for (unsigned char c : config_dir.string()) {
    hash = (hash ^ c) * 16777619u;
  }
  return std::format("{}-{:08x}.git", config_dir.filename().string(), hash);
}

class GitConfigHistory final : public ConfigHistory {
public:
  GitConfigHistory(std::filesystem::path git, std::filesystem::path config_dir, std::filesystem::path data_dir, std::string app_name)
    : git_(std::move(git)), config_dir_(std::move(config_dir)), repo_(data_dir / "config-history" / repo_name(config_dir_)),
      snapshots_(data_dir / "snapshots"), app_name_(std::move(app_name)) {}

  Recorded record(std::string_view message) override {
    if (!ensure_repo()) {
      return {std::nullopt, true};
    }
    exec({"add", "-A"});
    const bool has_head = query({"rev-parse", "-q", "--verify", "HEAD"}).exit_code == 0;
    if (has_head && trim(query({"status", "--porcelain"}).output).empty()) {
      return {version("HEAD"), false};
    }
    std::vector<std::string> commit{"commit", "-q", "-m", std::string(message)};
    if (!has_head) {
      commit.push_back("--allow-empty");
    }
    if (const auto r = exec(commit); r.exit_code != 0) {
      log::warn("config history: commit failed: {}", trim(r.output));
      return {std::nullopt, true};
    }
    return {version("HEAD"), true};
  }

  void mark(const std::string& hash, bool good) override {
    exec({"notes", "--ref=sailfish", "add", "-f", "-m", good ? "good" : "bad", "--end-of-options", hash});
    if (good) {
      exec({"update-ref", "--end-of-options", "refs/sailfish/good", hash});
    }
  }

  std::optional<ConfigVersion> last_good() override {
    if (query({"rev-parse", "-q", "--verify", "refs/sailfish/good^{commit}"}).exit_code != 0) {
      return std::nullopt;
    }
    return version("refs/sailfish/good");
  }

  std::vector<ConfigVersion> list(int limit) override {
    std::vector<ConfigVersion> versions;
    const auto r = query({"log", std::format("-n{}", limit), "--notes=sailfish", std::string(kFormat)});
    if (r.exit_code != 0) {
      log::warn("config history: cannot list versions (git log exited with {})", r.exit_code);
      return versions;
    }
    for (const auto& record : split(r.output, kRecord)) {
      if (auto v = parse_version(record)) {
        versions.push_back(std::move(*v));
      }
    }
    return versions;
  }

  std::string show(const std::string& hash) override {
    return query({"show", "--stat", "--patch", "--format=%H%n%ad%n%n    %s%n", "--date=local", "--end-of-options", hash}).output;
  }

  std::optional<std::filesystem::path> materialize(const std::string& hash) override {
    const auto root = snapshots_ / hash;
    const auto dir = root / app_name_;
    const auto marker = root / ".complete";
    std::error_code ec;
    if (std::filesystem::exists(marker, ec)) {
      return root;
    }
    std::filesystem::remove_all(root, ec);
    if (std::filesystem::create_directories(dir, ec); ec) {
      log::warn("config history: cannot create {}: {}", dir.string(), ec.message());
      return std::nullopt;
    }
    // A throwaway index keeps the history's own index untouched.
    const std::vector<std::pair<std::string, std::string>> env{{"GIT_INDEX_FILE", (root / ".index").string()}};
    if (exec({"read-tree", "--end-of-options", hash}, env, dir).exit_code != 0 || exec({"checkout-index", "-a", "-f"}, env, dir).exit_code != 0) {
      log::warn("config history: cannot check out {}", hash);
      return std::nullopt;
    }
    std::filesystem::remove(root / ".index", ec);
    std::ofstream(marker) << hash << '\n';
    prune_snapshots(snapshots_);
    return root;
  }

  bool restore(const std::string& hash) override {
    record(std::format("Before restoring {}", hash.substr(0, 8)));
    if (exec({"read-tree", "-u", "--reset", "--end-of-options", hash}).exit_code != 0) {
      return false;
    }
    record(std::format("Restored {}", hash.substr(0, 8)));
    return true;
  }

private:
  bool ensure_repo() {
    std::error_code ec;
    if (std::filesystem::exists(repo_ / "HEAD", ec)) {
      return true;
    }
    if (std::filesystem::create_directories(repo_ / "info", ec); ec) {
      log::warn("config history: cannot create {}: {}", repo_.string(), ec.message());
      return false;
    }
    if (exec({"init", "-q"}).exit_code != 0) {
      log::warn("config history: git init failed in {}", repo_.string());
      return false;
    }
    std::ofstream(repo_ / "info" / "exclude") << ".git\n.DS_Store\n*.swp\n*.swo\n*~\n.netrwhist\n";
    return true;
  }

  std::optional<ConfigVersion> version(const std::string& rev) {
    const auto r = query({"log", "-1", "--notes=sailfish", std::string(kFormat), rev});
    return r.exit_code == 0 ? parse_version(split(r.output, kRecord)[0]) : std::nullopt;
  }

  // stdout only: for commands whose output is parsed (git warns on stderr).
  platform::CapturedRun query(std::vector<std::string> args, std::vector<std::pair<std::string, std::string>> env = {},
    std::optional<std::filesystem::path> work_tree = std::nullopt) {
    return git(std::move(args), std::move(env), work_tree, platform::StderrMode::Discard);
  }

  // stdout and stderr together: for commands whose failures get logged.
  platform::CapturedRun exec(std::vector<std::string> args, std::vector<std::pair<std::string, std::string>> env = {},
    std::optional<std::filesystem::path> work_tree = std::nullopt) {
    return git(std::move(args), std::move(env), work_tree, platform::StderrMode::MergeIntoStdout);
  }

  platform::CapturedRun git(std::vector<std::string> args, std::vector<std::pair<std::string, std::string>> env,
    std::optional<std::filesystem::path> work_tree, platform::StderrMode stderr_mode) {
    // The user's git config must not leak in (hooks, signing, fsmonitor,
    // excludes, colors, diff drivers): it is ignored entirely, and the few
    // settings history needs are given here.
    std::vector<std::string> full{
      "--git-dir=" + repo_.string(),
      "--work-tree=" + work_tree.value_or(config_dir_).string(),
      "-c", "user.name=Sailfish",
      "-c", "user.email=sailfish@localhost",
      "-c", "core.hooksPath=" + (repo_ / "no-hooks").string(),
      "-c", "commit.gpgsign=false",
      "-c", "core.autocrlf=false",
      "-c", "core.quotepath=off",
      "-c", "init.defaultBranch=main",
    };
    full.insert(full.end(), args.begin(), args.end());
    env.emplace_back("GIT_CONFIG_GLOBAL", kNullDevice);
    env.emplace_back("GIT_CONFIG_NOSYSTEM", "1");
    env.emplace_back("GIT_TERMINAL_PROMPT", "0");
    env.emplace_back("GIT_OPTIONAL_LOCKS", "0");
    return platform::run_captured({.program = git_.string(), .args = std::move(full), .env = std::move(env), .stderr_mode = stderr_mode});
  }

  std::filesystem::path git_;
  std::filesystem::path config_dir_;
  std::filesystem::path repo_;
  std::filesystem::path snapshots_;
  std::string app_name_;
};

class UnavailableHistory final : public ConfigHistory {
public:
  // Without history every recording is treated as a change: reloads must
  // still happen, they just can't be rolled back.
  Recorded record(std::string_view) override { return {std::nullopt, true}; }
  void mark(const std::string&, bool) override {}
  std::optional<ConfigVersion> last_good() override { return std::nullopt; }
  std::vector<ConfigVersion> list(int) override { return {}; }
  std::string show(const std::string&) override { return {}; }
  std::optional<std::filesystem::path> materialize(const std::string&) override { return std::nullopt; }
  bool restore(const std::string&) override { return false; }
};

} // namespace

bool is_version_hash(std::string_view s) {
  return s.size() >= 7 && s.size() <= 40 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::shared_ptr<ConfigHistory> make_config_history(const std::filesystem::path& config_dir, const std::filesystem::path& data_dir,
  const std::string& app_name) {
  const auto git = platform::find_executable("git");
  if (!git) {
    log::warn("git is not on PATH; config history is disabled");
    return std::make_shared<UnavailableHistory>();
  }
  std::error_code ec;
  const auto real = std::filesystem::canonical(config_dir, ec);
  return std::make_shared<GitConfigHistory>(*git, ec ? config_dir : real, data_dir, app_name);
}

} // namespace sailfish::services
