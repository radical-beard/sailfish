#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "host/host_api.h"
#include "nvim/editor.h"
#include "nvim/instance.h"
#include "services/config_history.h"
#include "services/task_runner.h"

namespace sailfish::app {

// What the window needs to hear from the supervisor.
struct SupervisorEvents {
  std::function<void()> active_changed;                  // a different editor is now on screen
  std::function<void(const std::string&)> mode_changed;  // "" normally, or a label such as "safe mode"
  std::function<void()> message_ui_changed;              // the active editor's messages moved to or from a plugin
};

// How the supervisor reaches processes, history and time: the real ones in
// the app, fakes in tests.
struct SupervisorPorts {
  // Starts an editor attached at this size; throws platform::SpawnError.
  std::function<std::unique_ptr<nvim::Editor>(const nvim::LaunchOptions&, int cols, int rows)> launch;
  std::function<std::shared_ptr<services::ConfigHistory>(const std::filesystem::path& config_dir)> open_history;
  std::function<double()> clock;  // monotonic seconds
};

// Keeps one editor alive across config changes. It owns the Neovim
// instances: the active one on screen, and at most one candidate starting on
// new config. A candidate that starts cleanly replaces the active one,
// carrying over windows, buffers (unsaved edits and undo history included),
// registers and cursors; one that fails is discarded and its errors
// reported, so the user keeps working in the editor that still works. Every
// config that starts is versioned; one that starts cleanly becomes "last
// good". Every step has a deadline, and an editor that won't quit is killed.
class Supervisor {
public:
  Supervisor(nvim::LaunchOptions base, std::filesystem::path data_dir, SupervisorPorts ports, host::HostApi& api,
    services::TaskRunner& tasks, SupervisorEvents events);
  ~Supervisor();
  Supervisor(const Supervisor&) = delete;
  Supervisor& operator=(const Supervisor&) = delete;

  void start(int cols, int rows);
  [[nodiscard]] nvim::Editor& active() { return *active_.editor; }
  // Whether Sailfish draws the active editor's messages and cmdline (not
  // when a plugin such as noice draws them).
  [[nodiscard]] bool native_messages() const { return active_.native_messages; }
  // Applies queued messages from every editor; call when woken.
  void pump();
  // Acts on deadlines. Call on every loop iteration, sleeping no longer
  // than next_deadline() in between.
  void poll();
  [[nodiscard]] std::optional<double> next_deadline() const;
  // Keys typed while state moves to a candidate wait and follow it.
  void input(const std::string& keys);
  void resize(int cols, int rows);
  void set_focus(bool focused);
  // The window is closing. The runtime decides how (hot exit keeps unsaved
  // work; otherwise Neovim asks about it). Asked again while Neovim doesn't
  // respond, the supervisor ends it.
  void request_close();
  [[nodiscard]] bool finished() const { return active_.editor->exited(); }

private:
  // A request to move to new config. While one is carried out, newer ones
  // wait as a single pending request: the latest wins, and a forced reload
  // stays forced.
  struct Request {
    enum class Goal { Reload, LastGood } goal;
    std::string reason;
    bool force = false;
  };

  // How carrying out a request differs between goals, decided once.
  struct Plan {
    bool safe = false;           // start the last working config (a snapshot)
    bool marks_version = false;  // the outcome marks the version good or bad
    std::string failure_title;
  };

  struct Tracked {
    std::unique_ptr<nvim::Editor> editor;
    bool started = false;             // its runtime reported startup.done
    std::vector<std::string> errors;  // error messages seen before that
    std::string server;               // its RPC address (v:servername)
    bool native_messages = true;
  };

  // Moving state to a candidate: wait for Neovim to be in Normal mode, export
  // its state, start the candidate. Keys are held from the export on.
  struct Transfer {
    enum class Phase { CheckingIdle, WaitingForIdle, Exporting, Starting } phase = Phase::CheckingIdle;
    Plan plan;
    std::optional<services::ConfigVersion> version;
    std::optional<std::filesystem::path> snapshot;
    std::filesystem::path dir;
    double deadline = 0;  // of the current phase
    unsigned seq = 0;
  };

  struct Retiring {
    std::unique_ptr<nvim::Editor> editor;
    double since = 0;
    enum class Stage { Quitting, InputClosed, Killed } stage = Stage::Quitting;
  };

  static Plan plan_for(Request::Goal goal);
  void register_methods(host::HostApi& api);
  bool history_ready(rpc::Responder& responder);
  Tracked* tracked(const nvim::Editor& editor);
  void collect_errors(Tracked& t);
  void on_started(nvim::Editor& from, const msgpack::Value& info);
  void on_initial_started(const std::filesystem::path& config_dir, std::vector<std::string> errors);
  void request(Request r);
  void begin(Request r);
  void begin_transfer(Plan plan, std::optional<services::ConfigVersion> version, std::optional<std::filesystem::path> snapshot);
  void check_idle();
  void export_state();
  void start_candidate();
  void promote();
  void reject(std::vector<std::string> errors);
  void reject_candidate(std::string why);
  void record_outcome(const Transfer& transfer, bool started_cleanly);
  void cancel_transfer(const std::string& why);
  void finish();
  void retire(std::unique_ptr<nvim::Editor> editor);
  void poll_transfer(double now);
  void poll_retiring(double now);
  void poll_close(double now);
  void publish_server();
  void show_report(const std::string& title, const std::vector<std::string>& errors, msgpack::Array actions);
  [[nodiscard]] bool holding_input() const;

  nvim::LaunchOptions base_;
  std::filesystem::path data_dir_;
  SupervisorPorts ports_;
  services::TaskRunner& tasks_;
  SupervisorEvents events_;

  Tracked active_;
  std::optional<Tracked> candidate_;
  std::vector<Retiring> retiring_;
  std::shared_ptr<services::ConfigHistory> history_;
  std::filesystem::path config_dir_;

  bool busy_ = false;  // a request is being carried out
  std::optional<Request> pending_;
  std::optional<Transfer> transfer_;
  unsigned transfer_seq_ = 0;
  std::vector<std::string> held_input_;

  struct Closing {
    bool requested = false;                // the runtime is handling a close
    std::optional<double> probe_deadline;  // asked again: does Neovim still answer?
    std::optional<double> kill_at;         // terminated; killed if still running then
    bool ending = false;                   // Neovim didn't answer, so the supervisor is ending it
  } closing_;

  bool focused_ = true;
  int cols_ = 0;
  int rows_ = 0;
};

} // namespace sailfish::app
