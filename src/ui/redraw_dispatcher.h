#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "msgpack/value.h"
#include "ui/ui_state.h"

namespace sailfish::ui {

// Applies `redraw` batches to a UiState. This is the protocol boundary: each
// handler reads its whole event tuple before touching the state, so a tuple
// that fails a check (msgpack::TypeError, std::out_of_range) is logged and
// skipped, leaving the state and the rest of the batch as they were. Neovim
// may add parameters or events at any time; handlers read only what they need
// and unknown events are ignored.
//
// Neovim may split one redraw across several batches, and only the last ends
// in `flush`. Earlier ones are held and applied together with it, so the state
// only ever shows a complete redraw.
class RedrawDispatcher {
public:
  using Handler = std::function<void(UiState&, const msgpack::Array& args)>;

  // Each event has one handler; registering a second throws std::logic_error.
  void add(std::string event, Handler handler);
  void apply(UiState& state, const msgpack::Array& batch);

private:
  void apply_now(UiState& state, const msgpack::Array& batch);
  void apply_event(UiState& state, const msgpack::Value& event);

  std::unordered_map<std::string, Handler> handlers_;
  std::unordered_set<std::string> reported_unknown_;
  std::vector<msgpack::Array> held_;  // batches of a redraw whose flush hasn't arrived
};

// A dispatcher with every protocol area registered.
RedrawDispatcher make_redraw_dispatcher();

} // namespace sailfish::ui
