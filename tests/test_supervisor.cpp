#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "app/supervisor.h"
#include "platform/child_process.h"

using namespace sailfish;
using msgpack::Array;
using msgpack::Map;
using msgpack::Value;

namespace {

// An editor the test drives by hand: it records what the supervisor asks of
// it, and answers requests when the test says so.
class FakeEditor final : public nvim::Editor {
public:
  struct Call {
    std::string what;  // the API method, or the runtime entry point of an nvim_exec_lua dispatch
    Array args;
    rpc::ResponseHandler respond;
  };

  explicit FakeEditor(nvim::LaunchOptions launch_options) : options(std::move(launch_options)) {}

  void pump() override {}
  void input(std::string_view keys) override { inputs.emplace_back(keys); }
  void resize(int, int) override {}
  void set_focus(bool) override {}
  void request(std::string_view method, Array params, rpc::ResponseHandler respond) override {
    if (method == "nvim_exec_lua") {
      Array call = params.at(1).as_array();
      const std::string name = call.at(0).as_string();
      call.erase(call.begin());
      pending.push_back({name, std::move(call), std::move(respond)});
    } else {
      pending.push_back({std::string(method), std::move(params), std::move(respond)});
    }
    asked.push_back(pending.back().what);
  }
  void notify(std::string_view method, Array params) override { notified.push_back({std::string(method), std::move(params), {}}); }
  void close_input() override { input_closed = true; }
  void terminate() override { terminated = true; }
  void kill() override { killed = true; }
  [[nodiscard]] bool exited() const override { return gone; }
  [[nodiscard]] ui::UiState& ui() override { return state; }

  [[nodiscard]] bool waiting_on(std::string_view what) const {
    return std::any_of(pending.begin(), pending.end(), [&](const Call& c) { return c.what == what; });
  }
  [[nodiscard]] int times_asked(std::string_view what) const { return static_cast<int>(std::count(asked.begin(), asked.end(), what)); }
  // The arguments of the first unanswered call to `what`.
  [[nodiscard]] const Array& args_of(std::string_view what) const { return find(what)->args; }

  void answer(std::string_view what, Value result = {}) {
    rpc::Response r;
    r.result = std::move(result);
    respond(what, r);
  }
  void fail(std::string_view what, std::string message) {
    rpc::Response r;
    r.error = Value(Array{0, std::move(message)});
    respond(what, r);
  }

  nvim::LaunchOptions options;
  std::vector<Call> pending;
  std::vector<std::string> asked;
  std::vector<Call> notified;
  std::vector<std::string> inputs;
  bool input_closed = false;
  bool terminated = false;
  bool killed = false;
  bool gone = false;
  ui::UiState state;

private:
  std::vector<Call>::const_iterator find(std::string_view what) const {
    const auto it = std::find_if(pending.begin(), pending.end(), [&](const Call& c) { return c.what == what; });
    REQUIRE_MESSAGE(it != pending.end(), "nothing asked for ", what);
    return it;
  }
  void respond(std::string_view what, const rpc::Response& r) {
    const auto it = pending.begin() + (find(what) - pending.cbegin());
    const rpc::ResponseHandler handler = std::move(it->respond);
    pending.erase(it);
    handler(r);
  }
};

class FakeHistory final : public services::ConfigHistory {
public:
  Recorded record(std::string_view) override {
    ++records;
    return next_record;
  }
  void mark(const std::string& hash, bool good) override { marks.emplace_back(hash, good); }
  std::optional<services::ConfigVersion> last_good() override { return good; }
  std::vector<services::ConfigVersion> list(int) override { return {}; }
  std::string show(const std::string&) override { return {}; }
  std::optional<std::filesystem::path> materialize(const std::string&) override { return snapshot; }
  bool restore(const std::string&) override { return true; }

  Recorded next_record{services::ConfigVersion{"abcdef1", 1700000000, "Edited init.lua", ""}, true};
  std::optional<services::ConfigVersion> good;
  std::optional<std::filesystem::path> snapshot;
  int records = 0;
  std::vector<std::pair<std::string, bool>> marks;
};

// Runs background work for real, and lets the test wait for its results.
struct Tasks {
  std::mutex mutex;
  std::condition_variable woke;
  int wakes = 0;
  services::TaskRunner runner{[this] {
    std::lock_guard lock(mutex);
    ++wakes;
    woke.notify_all();
  }};

  // Delivers the next `count` completions.
  void settle(int count = 1) {
    for (int i = 0; i < count; ++i) {
      std::unique_lock lock(mutex);
      REQUIRE(woke.wait_for(lock, std::chrono::seconds(5), [&] { return wakes > 0; }));
      --wakes;
      lock.unlock();
      runner.drain();
    }
  }
};

const Value& global(const nvim::LaunchOptions& options, std::string_view key) {
  for (const auto& [k, v] : options.globals) {
    if (k.as_string() == key) {
      return v;
    }
  }
  FAIL("no global ", key);
  throw std::logic_error("unreachable");
}

const Map kIdle{{"mode", "n"}, {"blocking", false}};
const Map kBlocked{{"mode", "n"}, {"blocking", true}};

struct Rig {
  double now = 100;
  std::filesystem::path data_dir = std::filesystem::temp_directory_path() / "sailfish-supervisor-test";
  std::vector<FakeEditor*> launched;
  std::shared_ptr<FakeHistory> history = std::make_shared<FakeHistory>();
  bool spawn_fails = false;
  int active_changes = 0;
  std::vector<std::string> modes;
  int message_ui_changes = 0;
  host::HostApi api;
  Tasks tasks;
  std::unique_ptr<app::Supervisor> supervisor;

  Rig() {
    std::filesystem::remove_all(data_dir);
    nvim::LaunchOptions base;
    base.args = {"notes.txt"};
    app::SupervisorPorts ports{
      .launch = [this](const nvim::LaunchOptions& options, int, int) -> std::unique_ptr<nvim::Editor> {
        if (spawn_fails) {
          throw platform::SpawnError("cannot start 'nvim': No such file or directory");
        }
        auto editor = std::make_unique<FakeEditor>(options);
        launched.push_back(editor.get());
        return editor;
      },
      .open_history = [this](const std::filesystem::path&) { return history; },
      .clock = [this] { return now; },
    };
    supervisor = std::make_unique<app::Supervisor>(base, data_dir, ports, api, tasks.runner,
      app::SupervisorEvents{
        .active_changed = [this] { ++active_changes; },
        .mode_changed = [this](const std::string& mode) { modes.push_back(mode); },
        .message_ui_changed = [this] { ++message_ui_changes; },
      });
    supervisor->start(80, 24);
  }
  ~Rig() {
    supervisor.reset();
    std::filesystem::remove_all(data_dir);
  }

  FakeEditor& active() { return static_cast<FakeEditor&>(supervisor->active()); }

  // The runtime in `editor` reports that it finished starting.
  void started(FakeEditor& editor, std::vector<std::string> errors = {}) {
    api.on_host_notification(editor, "sailfish.startup.done",
      {Value(Map{{"config", "/home/me/.config/nvim"}, {"server", "/tmp/nvim.1"}, {"errors", Value(Array(errors.begin(), errors.end()))}})});
  }
  void boot() {
    started(*launched.at(0));
    tasks.settle();
  }
  void edit_config() { api.on_host_notification(active(), "sailfish.config.changed", {Value(Array{"/home/me/.config/nvim/init.lua"})}); }
  void action(std::string name) { api.on_host_notification(active(), "sailfish.config.action", {Value(std::move(name))}); }
  // Takes a recorded edit through the idle check and the export, up to the
  // candidate being launched.
  void export_after_record() {
    tasks.settle();
    active().answer("nvim_get_mode", kIdle);
    active().answer("state.handoff");
  }
  FakeEditor& candidate() { return *launched.back(); }
};

} // namespace

TEST_CASE("supervisor: a clean start is versioned and marked good") {
  Rig rig;
  rig.boot();
  CHECK(rig.history->records == 1);
  CHECK(rig.history->marks == std::vector<std::pair<std::string, bool>>{{"abcdef1", true}});
  CHECK_FALSE(rig.active().waiting_on("report.show"));
  std::ifstream server(rig.data_dir / "run" / "current-server");
  CHECK(std::string(std::istreambuf_iterator<char>(server), {}) == "/tmp/nvim.1");
}

TEST_CASE("supervisor: errors at startup are reported, with the last working config on offer") {
  Rig rig;
  rig.history->good = services::ConfigVersion{"1234567", 1600000000, "Started cleanly", "good"};
  rig.started(rig.active(), {"E5113: Error while calling lua chunk"});
  rig.tasks.settle();
  CHECK(rig.history->marks == std::vector<std::pair<std::string, bool>>{{"abcdef1", false}});
  const Value& report = rig.active().args_of("report.show").at(0);
  CHECK(report.find("errors")->as_array().at(0).as_string() == "E5113: Error while calling lua chunk");
  CHECK(report.find("actions")->as_array().at(0).find("action")->as_string() == "load_last_good");
}

TEST_CASE("supervisor: a config edit moves the editor to a candidate that starts cleanly") {
  Rig rig;
  rig.boot();
  FakeEditor& old = rig.active();
  rig.edit_config();
  rig.export_after_record();
  REQUIRE(rig.launched.size() == 2);
  FakeEditor& candidate = rig.candidate();
  CHECK(candidate.options.args.empty());
  CHECK(global(candidate.options, "mode").as_string() == "reload");
  CHECK(global(candidate.options, "config_root").as_string() == "/home/me/.config/nvim");

  rig.supervisor->input("ihello");
  CHECK(old.inputs.empty());  // held: the state already left `old`

  rig.started(candidate);
  CHECK(&rig.supervisor->active() == &candidate);
  CHECK(candidate.inputs == std::vector<std::string>{"ihello"});
  CHECK(candidate.waiting_on("report.reloaded"));
  CHECK(rig.active_changes == 1);
  CHECK(rig.modes == std::vector<std::string>{""});
  REQUIRE(old.notified.size() == 1);
  CHECK(old.notified[0].args.at(0).as_string().find("qall!") != std::string::npos);
  rig.tasks.settle();
  CHECK(rig.history->marks.back() == std::pair<std::string, bool>{"abcdef1", true});
}

TEST_CASE("supervisor: a candidate that reports errors is discarded; the working editor stays") {
  Rig rig;
  rig.boot();
  FakeEditor& old = rig.active();
  rig.edit_config();
  rig.export_after_record();
  rig.supervisor->input("x");
  rig.started(rig.candidate(), {"Sailfish feature 'autosave' failed to start: bad delay"});
  CHECK(&rig.supervisor->active() == &old);
  CHECK(old.inputs == std::vector<std::string>{"x"});
  CHECK(rig.candidate().notified.size() == 1);  // told to quit
  const Value& report = old.args_of("report.show").at(0);
  CHECK(report.find("title")->as_string().starts_with("Config reload failed"));
  CHECK(report.find("errors")->as_array().at(0).as_string() == "Sailfish feature 'autosave' failed to start: bad delay");
  rig.tasks.settle();
  CHECK(rig.history->marks.back() == std::pair<std::string, bool>{"abcdef1", false});
}

TEST_CASE("supervisor: rewriting the config with identical content doesn't reload") {
  Rig rig;
  rig.boot();
  rig.history->next_record.changed = false;
  rig.edit_config();
  rig.tasks.settle();
  CHECK(rig.active().times_asked("nvim_get_mode") == 0);
}

TEST_CASE("supervisor: the transfer waits for Normal mode, so keys typed meanwhile keep their meaning") {
  Rig rig;
  rig.boot();
  rig.edit_config();
  rig.tasks.settle();
  rig.active().answer("nvim_get_mode", Map{{"mode", "i"}, {"blocking", false}});
  rig.supervisor->input("text");
  CHECK(rig.active().inputs == std::vector<std::string>{"text"});
  CHECK_FALSE(rig.active().waiting_on("state.handoff"));
  rig.now += 0.3;
  rig.supervisor->poll();
  rig.active().answer("nvim_get_mode", kIdle);
  CHECK(rig.active().waiting_on("state.handoff"));
}

TEST_CASE("supervisor: while Neovim waits for a key in a prompt, the transfer waits too, and keys still reach it") {
  Rig rig;
  rig.boot();
  rig.edit_config();
  rig.tasks.settle();
  rig.active().answer("nvim_get_mode", kBlocked);
  rig.supervisor->input("y");
  CHECK(rig.active().inputs == std::vector<std::string>{"y"});
  CHECK_FALSE(rig.active().waiting_on("state.handoff"));
  REQUIRE(rig.supervisor->next_deadline());
  CHECK(*rig.supervisor->next_deadline() == doctest::Approx(100.25));

  rig.now += 0.3;
  rig.supervisor->poll();
  CHECK(rig.active().times_asked("nvim_get_mode") == 2);
  rig.active().answer("nvim_get_mode", kIdle);
  CHECK(rig.active().waiting_on("state.handoff"));
}

TEST_CASE("supervisor: an export Neovim never answers is given up, and the held keys go through") {
  Rig rig;
  rig.boot();
  rig.edit_config();
  rig.tasks.settle();
  rig.active().answer("nvim_get_mode", kIdle);
  rig.supervisor->input("x");
  CHECK(rig.active().inputs.empty());

  rig.now += 11;
  rig.supervisor->poll();
  CHECK(rig.active().inputs == std::vector<std::string>{"x"});
  CHECK(rig.active().waiting_on("report.show"));
  rig.active().answer("state.handoff");  // late: ignored
  CHECK(rig.launched.size() == 1);
}

TEST_CASE("supervisor: a candidate that never finishes starting is rejected") {
  Rig rig;
  rig.boot();
  rig.edit_config();
  rig.export_after_record();
  rig.now += 21;
  rig.supervisor->poll();
  CHECK(&rig.supervisor->active() == rig.launched[0]);
  const Value& report = rig.active().args_of("report.show").at(0);
  CHECK(report.find("errors")->as_array().back().as_string().find("did not finish starting") != std::string::npos);
}

TEST_CASE("supervisor: a candidate that exits while starting is rejected") {
  Rig rig;
  rig.boot();
  rig.edit_config();
  rig.export_after_record();
  rig.candidate().gone = true;
  rig.supervisor->poll();
  CHECK(&rig.supervisor->active() == rig.launched[0]);
  CHECK(rig.active().waiting_on("report.show"));
}

TEST_CASE("supervisor: when Neovim can't be started, the reload is rejected and typing goes on") {
  Rig rig;
  rig.boot();
  rig.spawn_fails = true;
  rig.edit_config();
  rig.export_after_record();
  const Value& report = rig.active().args_of("report.show").at(0);
  CHECK(report.find("errors")->as_array().at(0).as_string().find("No such file") != std::string::npos);
  rig.supervisor->input("j");
  CHECK(rig.active().inputs == std::vector<std::string>{"j"});
}

TEST_CASE("supervisor: requests arriving during a reload become one more, still forced") {
  Rig rig;
  rig.boot();
  rig.edit_config();
  rig.action("reload");  // forced
  rig.edit_config();     // not forced, but the merged request stays forced
  rig.export_after_record();
  CHECK(rig.history->records == 2);
  rig.history->next_record.changed = false;  // set before the pending request records
  rig.started(rig.candidate());

  rig.tasks.settle(2);  // the promotion's mark, then the pending request's record
  CHECK(rig.history->records == 3);
  CHECK(rig.active().waiting_on("nvim_get_mode"));  // forced past "unchanged"
}

TEST_CASE("supervisor: loading the last working config starts it in safe mode") {
  Rig rig;
  rig.boot();
  rig.history->good = services::ConfigVersion{"1234567", 1600000000, "Started cleanly", "good"};
  rig.history->snapshot = rig.data_dir / "snapshots" / "1234567";
  rig.action("load_last_good");
  rig.export_after_record();
  FakeEditor& candidate = rig.candidate();
  CHECK(global(candidate.options, "mode").as_string() == "safe");
  CHECK(std::find(candidate.options.env.begin(), candidate.options.env.end(),
          std::pair<std::string, std::string>{"XDG_CONFIG_HOME", (rig.data_dir / "snapshots" / "1234567").string()}) != candidate.options.env.end());
  rig.started(candidate);
  CHECK(rig.modes == std::vector<std::string>{"safe mode"});
}

TEST_CASE("supervisor: a replaced editor that won't quit has its input closed, then is killed") {
  Rig rig;
  rig.boot();
  FakeEditor& old = rig.active();
  rig.edit_config();
  rig.export_after_record();
  rig.started(rig.candidate());

  rig.now += 3.1;
  rig.supervisor->poll();
  CHECK(old.input_closed);
  CHECK_FALSE(old.killed);
  rig.now += 3;
  rig.supervisor->poll();
  CHECK(old.killed);
}

TEST_CASE("supervisor: closing again while Neovim doesn't answer ends it") {
  Rig rig;
  rig.boot();
  rig.supervisor->request_close();
  CHECK(rig.active().waiting_on("window.close_requested"));
  rig.supervisor->request_close();
  CHECK(rig.active().waiting_on("nvim_get_mode"));

  rig.now += 2.1;
  rig.supervisor->poll();
  CHECK(rig.active().terminated);
  rig.now += 1.1;
  rig.supervisor->poll();
  CHECK(rig.active().killed);
}

TEST_CASE("supervisor: closing again while Neovim shows its own prompt leaves it alone") {
  Rig rig;
  rig.boot();
  rig.supervisor->request_close();
  rig.supervisor->request_close();
  rig.active().answer("nvim_get_mode", kBlocked);
  rig.now += 10;
  rig.supervisor->poll();
  CHECK_FALSE(rig.active().terminated);
}

TEST_CASE("supervisor: who draws messages is tracked for each editor") {
  Rig rig;
  rig.boot();
  rig.edit_config();
  rig.export_after_record();
  rig.api.on_host_notification(rig.candidate(), "sailfish.ui.plugin_messages", {Value(true)});
  CHECK(rig.supervisor->native_messages());
  CHECK(rig.message_ui_changes == 0);
  rig.started(rig.candidate());
  CHECK_FALSE(rig.supervisor->native_messages());
}
