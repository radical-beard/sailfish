#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "msgpack/value.h"
#include "nvim/editor.h"
#include "platform/child_process.h"
#include "rpc/channel.h"
#include "ui/redraw_dispatcher.h"
#include "ui/ui_state.h"

namespace sailfish::nvim {

struct LaunchOptions {
  std::string program = "nvim";
  std::vector<std::string> pre_config_commands;  // --cmd, run before the user config
  std::vector<std::string> args;                 // files and flags forwarded from our command line
  std::vector<std::pair<std::string, std::string>> env;
  std::filesystem::path cwd;
  std::filesystem::path stderr_log;
  msgpack::Map ui_options;  // nvim_ui_attach options (ext_*)
  msgpack::Map globals;     // becomes g:sailfish (plus the host channel id)
};

// One embedded Neovim: its process, RPC channel, and the UI model built from
// its redraw events. Several can exist at once (hot reload runs a candidate
// next to the active instance), so nothing here is global.
class Instance final : public Editor, private rpc::MessageHandler {
public:
  // Spawns Neovim (throws platform::SpawnError) and attaches as its UI at
  // this size. Attaching completes asynchronously, which is what lets
  // Neovim go on to load the user's config.
  Instance(const LaunchOptions& options, int cols, int rows, std::function<void()> wake, HostSink& host);
  ~Instance() override;
  Instance(const Instance&) = delete;
  Instance& operator=(const Instance&) = delete;

  void pump() override;
  void input(std::string_view keys) override;
  void resize(int cols, int rows) override;
  void set_focus(bool focused) override;
  void request(std::string_view method, msgpack::Array params, rpc::ResponseHandler on_response) override;
  void notify(std::string_view method, msgpack::Array params) override;
  void close_input() override;
  void terminate() override;
  void kill() override;
  [[nodiscard]] bool exited() const override { return channel_->closed(); }
  [[nodiscard]] ui::UiState& ui() override { return ui_; }

private:
  void attach(int cols, int rows);
  void on_api_info(const rpc::Response& info);
  void send_input();
  void on_notification(std::string_view method, const msgpack::Array& params) override;
  void on_request(std::string_view method, const msgpack::Array& params, rpc::Responder responder) override;

  LaunchOptions options_;
  HostSink& host_;
  std::unique_ptr<platform::ChildProcess> process_;
  std::unique_ptr<rpc::Channel> channel_;
  ui::RedrawDispatcher redraw_;
  ui::UiState ui_;

  // Until nvim_ui_attach is sent, size and focus changes wait here.
  bool attached_ = false;
  std::pair<int, int> size_;
  std::optional<bool> focus_;

  // Keys reach Neovim in order, one nvim_input at a time: what a full input
  // queue didn't accept goes first in the next one.
  std::string unsent_input_;
  bool input_in_flight_ = false;
};

} // namespace sailfish::nvim
