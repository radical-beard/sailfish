#pragma once
#include <cstdint>
#include <format>
#include <limits>
#include <map>

#include "msgpack/value.h"
#include "ui/redraw_dispatcher.h"

// One registration function per area of the UI protocol (api-ui-events.txt).
// Supporting a new event means adding a handler in its area's file.
namespace sailfish::ui {

void register_global_events(RedrawDispatcher& d);
void register_grid_events(RedrawDispatcher& d);
void register_multigrid_events(RedrawDispatcher& d);
void register_cmdline_events(RedrawDispatcher& d);
void register_message_events(RedrawDispatcher& d);
void register_popupmenu_events(RedrawDispatcher& d);

// Neovim lays out at most 10000 columns; no real grid has a longer side.
inline constexpr int kMaxGridSide = 10000;

// Readers for event arguments, the redraw boundary's range checks. Each throws
// msgpack::TypeError for a value out of range, so the dispatcher skips the
// event as malformed and handlers can trust what they read.

// lo <= v <= hi.
inline int bounded_arg(const msgpack::Value& v, int lo, int hi) {
  const int64_t i = v.as_int();
  if (i < lo || i > hi) {
    throw msgpack::TypeError(std::format("{} is outside [{}, {}]", i, lo, hi));
  }
  return static_cast<int>(i);
}

inline int int_arg(const msgpack::Value& v) { return bounded_arg(v, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()); }

// A size, count or offset.
inline int count_arg(const msgpack::Value& v) { return bounded_arg(v, 0, std::numeric_limits<int>::max()); }

// A position in a sequence of `size` elements.
inline int index_arg(const msgpack::Value& v, int size) { return bounded_arg(v, 0, size - 1); }

// A grid seen in grid_resize, the only event that creates grids.
inline std::map<int, Grid>::iterator grid_entry(UiState& s, const msgpack::Value& v) {
  const auto it = s.grids.find(int_arg(v));
  if (it == s.grids.end()) {
    throw msgpack::TypeError(std::format("unknown grid {}", v.to_debug_string()));
  }
  return it;
}

inline Grid& grid_arg(UiState& s, const msgpack::Value& v) { return grid_entry(s, v)->second; }

// An attribute id defined by an earlier hl_attr_define, or 0 (the default).
inline int hl_arg(const UiState& s, const msgpack::Value& v) { return index_arg(v, s.highlights.size()); }

} // namespace sailfish::ui
