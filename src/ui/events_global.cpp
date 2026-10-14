#include <vector>

#include "ui/redraw_events.h"

namespace sailfish::ui {

namespace {

CursorShape parse_shape(const std::string& name) {
  if (name == "horizontal") {
    return CursorShape::Horizontal;
  }
  if (name == "vertical") {
    return CursorShape::Vertical;
  }
  return CursorShape::Block;
}

// Neovim leaves out the keys a mode doesn't use; ModeInfo's defaults stand for them.
ModeInfo parse_mode_info(const UiState& s, const msgpack::Value& map) {
  ModeInfo m;
  for (const auto& [key, value] : map.as_map()) {
    const std::string& k = key.as_string();
    if (k == "cursor_shape") {
      m.shape = parse_shape(value.as_string());
    } else if (k == "cell_percentage") {
      m.cell_percentage = bounded_arg(value, 0, 100);
    } else if (k == "attr_id") {
      m.attr_id = hl_arg(s, value);
    } else if (k == "blinkwait") {
      m.blinkwait = count_arg(value);
    } else if (k == "blinkon") {
      m.blinkon = count_arg(value);
    } else if (k == "blinkoff") {
      m.blinkoff = count_arg(value);
    }
  }
  return m;
}

void option_set(UiState& s, const msgpack::Array& a) {
  const std::string& name = a.at(0).as_string();
  const msgpack::Value& value = a.at(1);
  if (name == "guifont") {
    s.options.guifont = value.as_string();
    s.changes.font = true;
  } else if (name == "linespace") {
    s.options.linespace = int_arg(value);
    s.changes.font = true;
  }
}

} // namespace

void register_global_events(RedrawDispatcher& d) {
  d.add("set_title", [](UiState& s, const msgpack::Array& a) {
    s.title = a.at(0).as_string();
    s.changes.title = true;
  });
  d.add("mode_info_set", [](UiState& s, const msgpack::Array& a) {
    std::vector<ModeInfo> modes;
    for (const auto& info : a.at(1).as_array()) {
      modes.push_back(parse_mode_info(s, info));
    }
    s.modes = std::move(modes);
  });
  d.add("mode_change", [](UiState& s, const msgpack::Array& a) {
    const std::string& mode = a.at(0).as_string();
    const int index = index_arg(a.at(1), static_cast<int>(s.modes.size()));
    s.mode = mode;
    s.mode_index = index;
  });
  d.add("option_set", option_set);
  d.add("busy_start", [](UiState& s, const msgpack::Array&) { s.busy = true; });
  d.add("busy_stop", [](UiState& s, const msgpack::Array&) { s.busy = false; });
  d.add("flush", [](UiState& s, const msgpack::Array&) {
    s.changes.flushed = true;
    // Scrolls no viewport move claimed (inserting a line) animate nothing.
    for (auto& [id, grid] : s.grids) {
      grid.take_scrolled_out();
    }
  });
}

} // namespace sailfish::ui
