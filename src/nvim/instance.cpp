#include "nvim/instance.h"

#include <chrono>

#include "log/log.h"

namespace sailfish::nvim {

namespace {

constexpr auto kExitGrace = std::chrono::seconds(2);
constexpr auto kTerminateGrace = std::chrono::seconds(1);

void log_failure(std::string_view what, const rpc::Response& r) {
  if (!r.ok()) {
    log::error("{} failed: {}", what, r.error_message());
  }
}

platform::ProcessSpec process_spec(const LaunchOptions& options) {
  platform::ProcessSpec spec;
  spec.program = options.program;
  spec.args.push_back("--embed");
  for (const auto& cmd : options.pre_config_commands) {
    spec.args.push_back("--cmd");
    spec.args.push_back(cmd);
  }
  spec.args.insert(spec.args.end(), options.args.begin(), options.args.end());
  spec.env = options.env;
  spec.cwd = options.cwd;
  if (!options.stderr_log.empty()) {
    spec.stderr_mode = platform::StderrMode::File;
    spec.stderr_file = options.stderr_log;
  }
  return spec;
}

} // namespace

void call_runtime(Editor& editor, std::string_view name, msgpack::Array args, rpc::ResponseHandler on_response) {
  msgpack::Array call{name};
  for (auto& arg : args) {
    call.push_back(std::move(arg));
  }
  editor.request("nvim_exec_lua", {"return require('sailfish.host').dispatch(...)", msgpack::Value(std::move(call))}, std::move(on_response));
}

Instance::Instance(const LaunchOptions& options, int cols, int rows, std::function<void()> wake, HostSink& host)
  : options_(options), host_(host), process_(platform::ChildProcess::spawn(process_spec(options))),
    channel_(std::make_unique<rpc::Channel>(*process_, std::move(wake))), redraw_(ui::make_redraw_dispatcher()), size_(cols, rows) {
  attach(cols, rows);
}

Instance::~Instance() {
  // Neovim exits when its input ends. One that doesn't (stuck in a Lua loop
  // or a prompt) is terminated, then killed: the channel's threads can only
  // finish once the process is gone.
  channel_->close_input();
  if (!process_->wait_for(kExitGrace)) {
    log::warn("nvim did not exit; terminating it");
    process_->terminate();
    if (!process_->wait_for(kTerminateGrace)) {
      process_->kill();
      process_->wait();
    }
  }
  channel_.reset();
}

void Instance::attach(int cols, int rows) {
  size_ = {cols, rows};
  channel_->request("nvim_get_api_info", {}, [this](const rpc::Response& info) { on_api_info(info); });
}

void Instance::on_api_info(const rpc::Response& info) {
  if (!info.ok()) {
    log::error("nvim_get_api_info failed: {}", info.error_message());
    close_input();
    return;
  }
  msgpack::Map globals = options_.globals;
  globals.emplace_back("channel", info.result.as_array().at(0).as_int());
  channel_->request("nvim_set_var", {"sailfish", msgpack::Value(std::move(globals))}, [](const rpc::Response& r) { log_failure("setting g:sailfish", r); });
  channel_->request("nvim_ui_attach", {size_.first, size_.second, msgpack::Value(options_.ui_options)},
    [](const rpc::Response& r) { log_failure("nvim_ui_attach", r); });
  // Requests are handled in order, so everything from here on follows the attach.
  attached_ = true;
  if (focus_) {
    set_focus(*focus_);
  }
}

void Instance::pump() { channel_->dispatch(*this); }

void Instance::input(std::string_view keys) {
  unsent_input_ += keys;
  if (!input_in_flight_) {
    send_input();
  }
}

void Instance::send_input() {
  input_in_flight_ = true;
  std::string keys = std::exchange(unsent_input_, {});
  channel_->request("nvim_input", {keys}, [this, keys](const rpc::Response& r) {
    input_in_flight_ = false;
    if (!r.ok()) {
      log::error("nvim_input failed: {}", r.error_message());
    } else if (const auto accepted = static_cast<size_t>(r.result.as_int()); accepted < keys.size()) {
      unsent_input_.insert(0, keys.substr(accepted));
    }
    if (!unsent_input_.empty()) {
      send_input();
    }
  });
}

void Instance::resize(int cols, int rows) {
  size_ = {cols, rows};
  if (attached_) {
    channel_->request("nvim_ui_try_resize", {cols, rows}, [](const rpc::Response& r) { log_failure("nvim_ui_try_resize", r); });
  }
}

void Instance::set_focus(bool focused) {
  if (!attached_) {
    focus_ = focused;
    return;
  }
  channel_->request("nvim_ui_set_focus", {focused}, [](const rpc::Response& r) { log_failure("nvim_ui_set_focus", r); });
}

void Instance::request(std::string_view method, msgpack::Array params, rpc::ResponseHandler on_response) {
  channel_->request(method, std::move(params), std::move(on_response));
}

void Instance::notify(std::string_view method, msgpack::Array params) { channel_->notify(method, std::move(params)); }

void Instance::close_input() { channel_->close_input(); }

void Instance::terminate() { process_->terminate(); }

void Instance::kill() { process_->kill(); }

void Instance::on_notification(std::string_view method, const msgpack::Array& params) {
  if (method == "redraw") {
    redraw_.apply(ui_, params);
  } else {
    host_.on_host_notification(*this, method, params);
  }
}

void Instance::on_request(std::string_view method, const msgpack::Array& params, rpc::Responder responder) {
  host_.on_host_request(*this, method, params, std::move(responder));
}

} // namespace sailfish::nvim
