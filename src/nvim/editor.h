#pragma once
#include <string_view>

#include "msgpack/value.h"
#include "rpc/channel.h"
#include "ui/ui_state.h"

namespace sailfish::nvim {

// One running Neovim, as the supervisor drives it: nvim::Instance in the
// app, fakes in tests.
class Editor {
public:
  virtual ~Editor() = default;

  // Applies the messages that arrived since the last pump; call when woken.
  virtual void pump() = 0;
  virtual void input(std::string_view keys) = 0;
  virtual void resize(int cols, int rows) = 0;
  virtual void set_focus(bool focused) = 0;
  virtual void request(std::string_view method, msgpack::Array params, rpc::ResponseHandler on_response) = 0;
  virtual void notify(std::string_view method, msgpack::Array params) = 0;

  // Ending it, from gentlest to surest: Neovim exits when its input ends;
  // terminate() asks the process to stop; kill() ends it unconditionally.
  virtual void close_input() = 0;
  virtual void terminate() = 0;
  virtual void kill() = 0;
  // The process is gone and every message it sent has been pumped.
  [[nodiscard]] virtual bool exited() const = 0;

  [[nodiscard]] virtual ui::UiState& ui() = 0;
};

// Calls an entry point the runtime registered with host.handle() in Lua.
void call_runtime(Editor& editor, std::string_view name, msgpack::Array args, rpc::ResponseHandler on_response);

// What an editor can't handle itself: calls from Lua addressed to the host.
class HostSink {
public:
  virtual ~HostSink() = default;
  virtual void on_host_notification(Editor& from, std::string_view method, const msgpack::Array& params) = 0;
  virtual void on_host_request(Editor& from, std::string_view method, const msgpack::Array& params, rpc::Responder responder) = 0;
};

} // namespace sailfish::nvim
