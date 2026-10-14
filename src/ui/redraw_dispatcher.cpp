#include "ui/redraw_dispatcher.h"

#include <format>
#include <stdexcept>
#include <string_view>

#include "log/log.h"
#include "ui/redraw_events.h"

namespace sailfish::ui {

namespace {

bool ends_with_flush(const msgpack::Array& batch) {
  if (batch.empty() || !batch.back().is_array()) {
    return false;
  }
  const msgpack::Array& event = batch.back().as_array();
  return !event.empty() && event[0].is_string() && event[0].as_string() == "flush";
}

// The boundary's one recovery: a value that fails a check costs only itself
// (one event, or one tuple of an event), never the rest of the batch.
template <typename Apply>
void skip_if_malformed(std::string_view event, const msgpack::Value& value, const Apply& apply) {
  try {
    apply();
  } catch (const msgpack::TypeError& e) {
    log::warn("skipping malformed '{}' event: {}", event, e.what());
  } catch (const std::out_of_range&) {
    log::warn("skipping '{}' event with missing arguments: {}", event, value.to_debug_string());
  }
}

} // namespace

void RedrawDispatcher::add(std::string event, Handler handler) {
  if (!handlers_.try_emplace(event, std::move(handler)).second) {
    throw std::logic_error(std::format("redraw event '{}' registered twice", event));
  }
}

void RedrawDispatcher::apply(UiState& state, const msgpack::Array& batch) {
  if (!ends_with_flush(batch)) {
    held_.push_back(batch);
    return;
  }
  for (const msgpack::Array& held : held_) {
    apply_now(state, held);
  }
  held_.clear();
  apply_now(state, batch);
}

void RedrawDispatcher::apply_now(UiState& state, const msgpack::Array& batch) {
  for (const msgpack::Value& event : batch) {
    skip_if_malformed("redraw", event, [&] { apply_event(state, event); });
  }
}

void RedrawDispatcher::apply_event(UiState& state, const msgpack::Value& event) {
  const msgpack::Array& tuples = event.as_array();
  const std::string& name = tuples.at(0).as_string();
  const auto it = handlers_.find(name);
  if (it == handlers_.end()) {
    if (reported_unknown_.insert(name).second) {
      log::debug("ignoring redraw event '{}'", name);
    }
    return;
  }
  for (size_t i = 1; i < tuples.size(); ++i) {
    skip_if_malformed(name, tuples[i], [&] { it->second(state, tuples[i].as_array()); });
  }
}

RedrawDispatcher make_redraw_dispatcher() {
  RedrawDispatcher d;
  register_global_events(d);
  register_grid_events(d);
  register_multigrid_events(d);
  register_cmdline_events(d);
  register_message_events(d);
  register_popupmenu_events(d);
  return d;
}

} // namespace sailfish::ui
