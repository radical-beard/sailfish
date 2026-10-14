#include <limits>
#include <vector>

#include "ui/redraw_events.h"

namespace sailfish::ui {

namespace {

// A grid's placement, created on first use. A new window's events can come
// before its win_pos and even its grid_resize (margins do), so placements
// are keyed by id alone; the grid layer only draws those whose grid exists.
// Callers read their other arguments first, since creating one changes the state.
Placement& placement_for(UiState& s, const msgpack::Value& grid) { return s.placements[bounded_arg(grid, 1, std::numeric_limits<int>::max())]; }

void win_pos(UiState& s, const msgpack::Array& a) {
  const int row = int_arg(a.at(2));
  const int col = int_arg(a.at(3));
  const int width = bounded_arg(a.at(4), 0, kMaxGridSide);
  const int height = bounded_arg(a.at(5), 0, kMaxGridSide);
  Placement& p = placement_for(s, a.at(0));
  p.role = GridRole::Window;
  p.row = row;
  p.col = col;
  p.width = width;
  p.height = height;
  p.zindex = 0;
  p.visible = true;
}

// screen_row and screen_col are where Neovim placed the float; the anchor
// arguments only matter to UIs that position floats themselves.
void win_float_pos(UiState& s, const msgpack::Array& a) {
  const Grid& grid = grid_arg(s, a.at(0));
  const int zindex = int_arg(a.at(7));
  const int compindex = int_arg(a.at(8));
  const int row = int_arg(a.at(9));
  const int col = int_arg(a.at(10));
  Placement& p = placement_for(s, a.at(0));
  p.role = GridRole::Float;
  p.row = row;
  p.col = col;
  p.width = grid.width();
  p.height = grid.height();
  p.zindex = zindex;
  p.compindex = compindex;
  p.visible = true;
}

void win_viewport(UiState& s, const msgpack::Array& a) {
  Grid& grid = grid_arg(s, a.at(0));
  const int64_t scroll_delta = a.at(7).as_int();
  Viewport& v = placement_for(s, a.at(0)).viewport;
  // The batch's grid_scroll events already moved the content; what they
  // destroyed is the old view, if it matches the move exactly.
  Grid::ScrolledOut destroyed = grid.take_scrolled_out();
  v.scroll_delta = scroll_delta;
  if (scroll_delta != 0) {
    ++v.scroll_seq;
    v.scrolled_out = destroyed.scrolled_rows == scroll_delta ? std::move(destroyed.rows) : std::vector<std::vector<Cell>>{};
  }
}

} // namespace

void register_multigrid_events(RedrawDispatcher& d) {
  d.add("win_pos", win_pos);
  d.add("win_float_pos", win_float_pos);
  d.add("win_external_pos", [](UiState& s, const msgpack::Array& a) {
    Placement& p = placement_for(s, a.at(0));
    p.role = GridRole::External;
    p.visible = false;  // external OS windows are not supported yet
  });
  d.add("win_hide", [](UiState& s, const msgpack::Array& a) {
    if (const auto it = s.placements.find(int_arg(a.at(0))); it != s.placements.end()) {
      it->second.visible = false;
    }
  });
  d.add("win_close", [](UiState& s, const msgpack::Array& a) { s.placements.erase(int_arg(a.at(0))); });
  d.add("win_viewport", win_viewport);
  d.add("win_viewport_margins", [](UiState& s, const msgpack::Array& a) {
    const Margins margins{count_arg(a.at(2)), count_arg(a.at(3)), count_arg(a.at(4)), count_arg(a.at(5))};
    placement_for(s, a.at(0)).margins = margins;
  });
}

} // namespace sailfish::ui
