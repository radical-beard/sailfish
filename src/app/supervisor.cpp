#include "app/supervisor.h"

#include <algorithm>
#include <ctime>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "log/log.h"
#include "platform/files.h"

namespace sailfish::app {

namespace {

constexpr double kIdleCheckSeconds = 5;       // nvim_get_mode is answered even mid-prompt
constexpr double kIdleRecheckSeconds = 0.25;  // until Neovim is back in Normal mode
constexpr double kExportSeconds = 10;
constexpr double kStartupSeconds = 20;
constexpr double kRetireGraceSeconds = 3;
constexpr double kCloseProbeSeconds = 2;
constexpr double kTerminateGraceSeconds = 1;

std::string text_of(const ui::Message& m) {
  std::string s;
  for (const auto& chunk : m.content) {
    s += chunk.text;
  }
  return s;
}

std::string describe_time(int64_t unix_seconds) {
  const std::time_t t = static_cast<std::time_t>(unix_seconds);
  char buffer[64];
  std::strftime(buffer, sizeof(buffer), "%b %d %H:%M", std::localtime(&t));
  return buffer;
}

msgpack::Value action(std::string key, std::string label, std::string name) {
  return msgpack::Value(msgpack::Map{{"key", std::move(key)}, {"label", std::move(label)}, {"action", std::move(name)}});
}

// A field the runtime's payload must carry.
const msgpack::Value& field(const msgpack::Value& payload, std::string_view key) {
  const msgpack::Value* value = payload.find(key);
  if (!value) {
    throw msgpack::TypeError(std::format("missing field '{}'", key));
  }
  return *value;
}

// A version hash from Lua, before it reaches git.
std::string version_hash(const msgpack::Value& value) {
  const std::string& hash = value.as_string();
  if (!services::is_version_hash(hash)) {
    throw msgpack::TypeError(std::format("'{}' is not a config version", hash));
  }
  return hash;
}

rpc::ResponseHandler log_failure(std::string what) {
  return [what = std::move(what)](const rpc::Response& r) {
    if (!r.ok()) {
      log::error("{} failed: {}", what, r.error_message());
    }
  };
}

} // namespace

Supervisor::Supervisor(nvim::LaunchOptions base, std::filesystem::path data_dir, SupervisorPorts ports, host::HostApi& api,
  services::TaskRunner& tasks, SupervisorEvents events)
  : base_(std::move(base)), data_dir_(std::move(data_dir)), ports_(std::move(ports)), tasks_(tasks), events_(std::move(events)) {
  register_methods(api);
}

Supervisor::~Supervisor() {
  // Tools find the running editor through this file; don't leave a stale
  // address (unless another Sailfish has published its own since).
  const auto file = data_dir_ / "run" / "current-server";
  std::error_code ec;
  if (std::ifstream in(file); in && std::string(std::istreambuf_iterator<char>(in), {}) == active_.server) {
    std::filesystem::remove(file, ec);
  }
}

Supervisor::Plan Supervisor::plan_for(Request::Goal goal) {
  switch (goal) {
    case Request::Goal::Reload:
      return {false, true, "Config reload failed: you are still on the previous working config"};
    case Request::Goal::LastGood:
      return {true, false, "The last working config failed to start"};
  }
  throw std::logic_error("unknown reload goal");
}

// History needs the config's path, which the first editor reports when it starts.
bool Supervisor::history_ready(rpc::Responder& responder) {
  if (!history_) {
    responder.fail("config history becomes available once the config has loaded");
  }
  return history_ != nullptr;
}

void Supervisor::register_methods(host::HostApi& api) {
  api.on_notification("sailfish.startup.done", [this](nvim::Editor& from, const msgpack::Array& params) { on_started(from, params.at(0)); });
  api.on_notification("sailfish.config.changed", [this](nvim::Editor& from, const msgpack::Array& params) {
    if (&from != active_.editor.get()) {
      return;
    }
    std::string files;
    for (const auto& f : params.at(0).as_array()) {
      files += (files.empty() ? "" : ", ") + std::filesystem::path(f.as_string()).filename().string();
    }
    request({Request::Goal::Reload, "Edited " + files});
  });
  api.on_notification("sailfish.config.action", [this](nvim::Editor& from, const msgpack::Array& params) {
    if (&from != active_.editor.get()) {
      return;
    }
    const std::string& name = params.at(0).as_string();
    if (name == "reload") {
      request({Request::Goal::Reload, "Reloaded by request", true});
    } else if (name == "load_last_good") {
      request({Request::Goal::LastGood, "Loaded the last working config", true});
    } else {
      throw msgpack::TypeError(std::format("unknown config action '{}'", name));
    }
  });
  api.on_notification("sailfish.ui.plugin_messages", [this](nvim::Editor& from, const msgpack::Array& params) {
    Tracked* t = tracked(from);
    if (!t) {
      return;  // an editor on its way out
    }
    t->native_messages = !params.at(0).as_bool();
    if (t == &active_) {
      events_.message_ui_changed();
    }
  });

  api.on_request("sailfish.config.history", [this](nvim::Editor&, const msgpack::Array& params, rpc::Responder responder) {
    const int64_t limit = params.empty() ? 50 : params[0].as_int();
    if (limit < 1 || limit > 1000) {
      throw msgpack::TypeError(std::format("history limit {} is out of range", limit));
    }
    if (!history_ready(responder)) {
      return;
    }
    tasks_.run([history = history_, limit] { return history->list(static_cast<int>(limit)); },
      [responder = std::make_shared<rpc::Responder>(std::move(responder))](std::vector<services::ConfigVersion> versions) {
        msgpack::Array list;
        for (const auto& v : versions) {
          list.push_back(msgpack::Value(msgpack::Map{{"hash", v.hash}, {"time", v.time}, {"message", v.message}, {"status", v.status}}));
        }
        responder->ok(msgpack::Value(std::move(list)));
      });
  });
  api.on_request("sailfish.config.show", [this](nvim::Editor&, const msgpack::Array& params, rpc::Responder responder) {
    const std::string hash = version_hash(params.at(0));
    if (!history_ready(responder)) {
      return;
    }
    tasks_.run([history = history_, hash] { return history->show(hash); },
      [responder = std::make_shared<rpc::Responder>(std::move(responder))](std::string text) { responder->ok(text); });
  });
  api.on_request("sailfish.config.restore", [this](nvim::Editor&, const msgpack::Array& params, rpc::Responder responder) {
    const std::string hash = version_hash(params.at(0));
    if (!history_ready(responder)) {
      return;
    }
    tasks_.run([history = history_, hash] { return history->restore(hash); },
      [this, hash, responder = std::make_shared<rpc::Responder>(std::move(responder))](bool ok) {
        responder->ok(ok);
        // The restore recorded itself as a version, so the watcher's reload
        // would see nothing new: reload explicitly.
        if (ok) {
          request({Request::Goal::Reload, "Restored " + hash.substr(0, 8), true});
        }
      });
  });
}

void Supervisor::start(int cols, int rows) {
  cols_ = cols;
  rows_ = rows;
  active_ = Tracked{.editor = ports_.launch(base_, cols, rows)};
}

Supervisor::Tracked* Supervisor::tracked(const nvim::Editor& editor) {
  if (&editor == active_.editor.get()) {
    return &active_;
  }
  if (candidate_ && &editor == candidate_->editor.get()) {
    return &*candidate_;
  }
  return nullptr;
}

void Supervisor::collect_errors(Tracked& t) {
  auto& unread = t.editor->ui().messages.unread;
  for (const auto& m : unread) {
    if (!m.is_error()) {
      continue;
    }
    if (t.started) {
      // Later errors go to the runtime's activity timeline (:Sailfish trace).
      nvim::call_runtime(*t.editor, "tracer.record_error", {text_of(m)}, log_failure("recording an error"));
    } else {
      t.errors.push_back(text_of(m));
    }
  }
  unread.clear();
}

void Supervisor::pump() {
  active_.editor->pump();
  collect_errors(active_);
  if (candidate_) {
    candidate_->editor->pump();
    // Its startup report may have promoted or rejected it just now.
    if (candidate_) {
      collect_errors(*candidate_);
    }
  }
  for (size_t i = 0; i < retiring_.size(); ++i) {
    retiring_[i].editor->pump();
  }
  std::erase_if(retiring_, [](const Retiring& old) { return old.editor->exited(); });
}

void Supervisor::poll() {
  const double now = ports_.clock();
  poll_transfer(now);
  poll_retiring(now);
  poll_close(now);
}

std::optional<double> Supervisor::next_deadline() const {
  std::optional<double> next;
  const auto consider = [&next](double t) { next = next ? std::min(*next, t) : t; };
  if (transfer_) {
    consider(transfer_->deadline);
  }
  for (const auto& old : retiring_) {
    if (old.stage == Retiring::Stage::Quitting) {
      consider(old.since + kRetireGraceSeconds);
    } else if (old.stage == Retiring::Stage::InputClosed) {
      consider(old.since + 2 * kRetireGraceSeconds);
    }
  }
  if (closing_.probe_deadline) {
    consider(*closing_.probe_deadline);
  }
  if (closing_.kill_at) {
    consider(*closing_.kill_at);
  }
  return next;
}

void Supervisor::input(const std::string& keys) {
  if (holding_input()) {
    held_input_.push_back(keys);
  } else {
    active_.editor->input(keys);
  }
}

void Supervisor::resize(int cols, int rows) {
  cols_ = cols;
  rows_ = rows;
  active_.editor->resize(cols, rows);
  if (candidate_) {
    candidate_->editor->resize(cols, rows);
  }
}

void Supervisor::set_focus(bool focused) {
  focused_ = focused;
  active_.editor->set_focus(focused);
}

void Supervisor::request_close() {
  if (closing_.requested) {
    // Asked again while the first request is unanswered. A Neovim that is
    // only showing a prompt (the close's own "save changes?") answers
    // nvim_get_mode; one that doesn't is stuck and gets ended.
    if (!closing_.probe_deadline && !closing_.ending) {
      closing_.probe_deadline = ports_.clock() + kCloseProbeSeconds;
      active_.editor->request("nvim_get_mode", {}, [this](const rpc::Response&) { closing_.probe_deadline.reset(); });
    }
    return;
  }
  closing_.requested = true;
  nvim::call_runtime(*active_.editor, "window.close_requested", {}, [this, editor = active_.editor.get()](const rpc::Response& r) {
    if (editor != active_.editor.get() || closing_.ending) {
      return;
    }
    closing_.requested = false;
    if (!r.ok()) {
      log::warn("closing through the runtime failed ({}); asking Neovim directly", r.error_message());
      editor->input("<C-\\><C-N>:confirm qa<CR>");
    }
  });
}

void Supervisor::on_started(nvim::Editor& from, const msgpack::Value& info) {
  Tracked* t = tracked(from);
  if (!t || t->started) {
    return;
  }
  const std::string server = field(info, "server").as_string();
  const std::filesystem::path config = field(info, "config").as_string();
  std::vector<std::string> reported;
  for (const auto& error : field(info, "errors").as_array()) {
    reported.push_back(error.as_string());
  }

  collect_errors(*t);
  t->started = true;
  t->server = server;
  t->errors.insert(t->errors.end(), reported.begin(), reported.end());
  if (t == &active_) {
    publish_server();
    on_initial_started(config, active_.errors);
  } else if (t->errors.empty()) {
    promote();
  } else {
    reject(t->errors);
  }
}

void Supervisor::on_initial_started(const std::filesystem::path& config_dir, std::vector<std::string> errors) {
  config_dir_ = config_dir;
  history_ = ports_.open_history(config_dir);
  const bool clean = errors.empty();
  struct Outcome {
    services::ConfigHistory::Recorded recorded;
    std::optional<services::ConfigVersion> last_good;
  };
  tasks_.run(
    [history = history_, clean] {
      Outcome o{history->record(clean ? "Started cleanly" : "Started with errors"), std::nullopt};
      if (o.recorded.version) {
        history->mark(o.recorded.version->hash, clean);
      }
      o.last_good = history->last_good();
      return o;
    },
    [this, errors = std::move(errors)](Outcome o) {
      if (errors.empty()) {
        return;
      }
      msgpack::Array actions;
      if (o.last_good && (!o.recorded.version || o.last_good->hash != o.recorded.version->hash)) {
        actions.push_back(action("l", "Load the last working config (" + describe_time(o.last_good->time) + ")", "load_last_good"));
      }
      actions.push_back(action("r", "Reload the config (after fixing it)", "reload"));
      show_report("Your config reported errors while starting", errors, std::move(actions));
    });
}

void Supervisor::request(Request r) {
  if (!history_) {
    log::warn("ignoring '{}': the config hasn't finished loading", r.reason);
    return;
  }
  if (busy_) {
    r.force = r.force || (pending_ && pending_->force);
    pending_ = std::move(r);
    return;
  }
  begin(std::move(r));
}

void Supervisor::begin(Request r) {
  busy_ = true;
  if (r.goal == Request::Goal::LastGood) {
    struct Snapshot {
      std::optional<services::ConfigVersion> version;
      std::optional<std::filesystem::path> root;
    };
    tasks_.run(
      [history = history_] {
        Snapshot s{history->last_good(), std::nullopt};
        if (s.version) {
          s.root = history->materialize(s.version->hash);
        }
        return s;
      },
      [this](Snapshot s) {
        if (!s.root) {
          show_report("No working config has been recorded yet", {}, {});
          finish();
          return;
        }
        begin_transfer(plan_for(Request::Goal::LastGood), s.version, s.root);
      });
    return;
  }
  tasks_.run([history = history_, reason = r.reason] { return history->record(reason); },
    [this, force = r.force](services::ConfigHistory::Recorded recorded) {
      // Rewriting a file with identical content (lazy.nvim's lockfile does)
      // is not a config change.
      if (!recorded.changed && !force) {
        finish();
        return;
      }
      begin_transfer(plan_for(Request::Goal::Reload), recorded.version, std::nullopt);
    });
}

void Supervisor::begin_transfer(Plan plan, std::optional<services::ConfigVersion> version, std::optional<std::filesystem::path> snapshot) {
  ++transfer_seq_;
  transfer_ = Transfer{.plan = std::move(plan),
    .version = std::move(version),
    .snapshot = std::move(snapshot),
    .dir = data_dir_ / "transfer" / std::to_string(transfer_seq_),
    .seq = transfer_seq_};
  check_idle();
}

// The transfer waits for plain Normal mode. Mid-command, mid-insert or in a
// prompt, the keys typed next belong to that mode; after the swap they
// would land in Normal mode of the new editor. And a Neovim blocked in a
// modal prompt runs no export until it gets a key, while keys are held
// during the export. nvim_get_mode is answered even then.
void Supervisor::check_idle() {
  transfer_->phase = Transfer::Phase::CheckingIdle;
  transfer_->deadline = ports_.clock() + kIdleCheckSeconds;
  active_.editor->request("nvim_get_mode", {}, [this, seq = transfer_->seq](const rpc::Response& r) {
    if (!transfer_ || transfer_->seq != seq || transfer_->phase != Transfer::Phase::CheckingIdle) {
      return;
    }
    if (!r.ok()) {
      cancel_transfer("Neovim did not report its mode: " + r.error_message());
    } else if (field(r.result, "blocking").as_bool() || field(r.result, "mode").as_string() != "n") {
      transfer_->phase = Transfer::Phase::WaitingForIdle;
      transfer_->deadline = ports_.clock() + kIdleRecheckSeconds;
    } else {
      export_state();
    }
  });
}

void Supervisor::export_state() {
  transfer_->phase = Transfer::Phase::Exporting;
  transfer_->deadline = ports_.clock() + kExportSeconds;
  nvim::call_runtime(*active_.editor, "state.handoff", {transfer_->dir.string()}, [this, seq = transfer_->seq](const rpc::Response& r) {
    if (!transfer_ || transfer_->seq != seq) {
      return;  // given up on while Neovim was busy
    }
    if (!r.ok()) {
      cancel_transfer("Sailfish could not save the editor state: " + r.error_message());
      return;
    }
    start_candidate();
  });
}

void Supervisor::start_candidate() {
  nvim::LaunchOptions options = base_;
  options.args.clear();  // the imported session reopens the files
  options.globals.emplace_back("import", transfer_->dir.string());
  options.globals.emplace_back("config_root", config_dir_.string());
  options.globals.emplace_back("mode", transfer_->plan.safe ? "safe" : "reload");
  if (transfer_->snapshot) {
    options.env.emplace_back("XDG_CONFIG_HOME", transfer_->snapshot->string());
  }
  try {
    candidate_ = Tracked{.editor = ports_.launch(options, cols_, rows_)};
  } catch (const platform::SpawnError& e) {
    reject({std::format("Neovim could not be started: {}", e.what())});
    return;
  }
  transfer_->phase = Transfer::Phase::Starting;
  transfer_->deadline = ports_.clock() + kStartupSeconds;
}

void Supervisor::promote() {
  const Transfer transfer = std::move(*transfer_);
  retire(std::move(active_.editor));
  active_ = std::move(*candidate_);
  candidate_.reset();
  active_.editor->set_focus(focused_);
  publish_server();
  events_.active_changed();
  events_.mode_changed(transfer.plan.safe ? "safe mode" : "");

  record_outcome(transfer, true);
  msgpack::Map info{{"safe", transfer.plan.safe}};
  if (transfer.version) {
    info.emplace_back("version", transfer.version->hash.substr(0, 8));
    info.emplace_back("time", describe_time(transfer.version->time));
  }
  nvim::call_runtime(*active_.editor, "report.reloaded", {msgpack::Value(std::move(info))}, log_failure("reporting the reload"));
  log::info("swapped to a Neovim running the {} config", transfer.plan.safe ? "last working" : "new");
  finish();
}

void Supervisor::reject(std::vector<std::string> errors) {
  log::warn("discarding the new config's Neovim: {} error(s)", errors.size());
  const Transfer transfer = std::move(*transfer_);
  if (candidate_) {
    retire(std::move(candidate_->editor));
    candidate_.reset();
  }
  record_outcome(transfer, false);
  show_report(transfer.plan.failure_title, errors, msgpack::Array{action("r", "Retry the reload (after fixing it)", "reload")});
  finish();
}

// What it reported so far, then why it's being given up on.
void Supervisor::reject_candidate(std::string why) {
  auto errors = candidate_->errors;
  errors.push_back(std::move(why));
  reject(std::move(errors));
}

void Supervisor::record_outcome(const Transfer& transfer, bool started_cleanly) {
  if (transfer.plan.marks_version && transfer.version) {
    tasks_.run([history = history_, hash = transfer.version->hash, started_cleanly] { history->mark(hash, started_cleanly); });
  }
}

// The active editor didn't cooperate in time (stuck, or busy in a prompt).
// Nothing was started, so it carries on, with the held keys.
void Supervisor::cancel_transfer(const std::string& why) {
  log::warn("reload cancelled: {}", why);
  show_report("The config was not reloaded", {why}, msgpack::Array{action("r", "Try again", "reload")});
  finish();
}

void Supervisor::finish() {
  busy_ = false;
  transfer_.reset();
  for (const auto& keys : held_input_) {
    active_.editor->input(keys);
  }
  held_input_.clear();
  std::error_code ec;
  std::filesystem::remove_all(data_dir_ / "transfer", ec);
  if (pending_) {
    Request next = std::move(*pending_);
    pending_.reset();
    begin(std::move(next));
  }
}

void Supervisor::retire(std::unique_ptr<nvim::Editor> editor) {
  // Its state now lives elsewhere: quit discarding changes, which also
  // removes its swap files. (Ending its input instead would make Neovim
  // "preserve" them, and the next start would ask about recovery.)
  editor->notify("nvim_command", {"let g:sailfish_retiring = 1 | qall!"});
  retiring_.push_back({std::move(editor), ports_.clock()});
}

void Supervisor::poll_transfer(double now) {
  if (!transfer_) {
    return;
  }
  switch (transfer_->phase) {
    case Transfer::Phase::CheckingIdle:
      if (now >= transfer_->deadline) {
        cancel_transfer("Neovim is not responding.");
      }
      break;
    case Transfer::Phase::WaitingForIdle:
      if (now >= transfer_->deadline) {
        check_idle();
      }
      break;
    case Transfer::Phase::Exporting:
      if (now >= transfer_->deadline) {
        cancel_transfer("Neovim did not save its state in time (is it busy?).");
      }
      break;
    case Transfer::Phase::Starting:
      if (candidate_->editor->exited()) {
        reject_candidate("Neovim exited while starting with the new config.");
      } else if (now >= transfer_->deadline) {
        reject_candidate(std::format("The new config did not finish starting within {:.0f} seconds (is it waiting for input?).", kStartupSeconds));
      }
      break;
  }
}

void Supervisor::poll_retiring(double now) {
  for (auto& old : retiring_) {
    if (old.stage == Retiring::Stage::Quitting && now >= old.since + kRetireGraceSeconds) {
      // It ignored :qall! (a blocking prompt): end its input.
      old.editor->close_input();
      old.stage = Retiring::Stage::InputClosed;
    } else if (old.stage == Retiring::Stage::InputClosed && now >= old.since + 2 * kRetireGraceSeconds) {
      // Still running: stuck (a Lua loop), and nothing else can end it.
      log::warn("a replaced Neovim would not exit; killing it");
      old.editor->kill();
      old.stage = Retiring::Stage::Killed;
    }
  }
}

void Supervisor::poll_close(double now) {
  if (closing_.probe_deadline && now >= *closing_.probe_deadline) {
    log::warn("Neovim is not responding; terminating it so the window can close");
    closing_.probe_deadline.reset();
    closing_.ending = true;
    active_.editor->terminate();
    closing_.kill_at = now + kTerminateGraceSeconds;
  } else if (closing_.kill_at && now >= *closing_.kill_at) {
    closing_.kill_at.reset();
    active_.editor->kill();
  }
}

bool Supervisor::holding_input() const {
  return transfer_ && (transfer_->phase == Transfer::Phase::Exporting || transfer_->phase == Transfer::Phase::Starting);
}

// Tools that talk to the running editor (`nvim --server`, tests) find the
// active instance here; the address changes with every hot reload.
void Supervisor::publish_server() {
  if (!platform::replace_file(data_dir_ / "run" / "current-server", active_.server)) {
    log::warn("cannot publish the server address under {}", data_dir_.string());
  }
}

void Supervisor::show_report(const std::string& title, const std::vector<std::string>& errors, msgpack::Array actions) {
  msgpack::Array error_list(errors.begin(), errors.end());
  msgpack::Map report{
    {"title", title},
    {"errors", msgpack::Value(std::move(error_list))},
    {"actions", msgpack::Value(std::move(actions))},
    {"config_root", config_dir_.string()},
  };
  nvim::call_runtime(*active_.editor, "report.show", {msgpack::Value(std::move(report))}, log_failure("showing a report"));
}

} // namespace sailfish::app
