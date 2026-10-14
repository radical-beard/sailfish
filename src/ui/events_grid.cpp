#include <format>
#include <optional>
#include <string_view>
#include <vector>

#include "ui/redraw_events.h"

namespace sailfish::ui {

namespace {

// Far above the ids a session defines (they are dense), yet low enough that a
// bad id can't exhaust memory.
constexpr int kMaxHlId = 1 << 20;

Rgb rgb_arg(const msgpack::Value& v) { return static_cast<Rgb>(bounded_arg(v, 0, 0xffffff)); }

HlAttr parse_rgb_attr(const msgpack::Value& map) {
  HlAttr h;
  for (const auto& [key, value] : map.as_map()) {
    const std::string& k = key.as_string();
    if (k == "foreground") {
      h.fg = rgb_arg(value);
    } else if (k == "background") {
      h.bg = rgb_arg(value);
    } else if (k == "special") {
      h.sp = rgb_arg(value);
    } else if (k == "reverse") {
      h.reverse = value.as_bool();
    } else if (k == "italic") {
      h.italic = value.as_bool();
    } else if (k == "bold") {
      h.bold = value.as_bool();
    } else if (k == "strikethrough") {
      h.strikethrough = value.as_bool();
    } else if (k == "overline") {
      h.overline = value.as_bool();
    } else if (k == "underline") {
      h.underline = Underline::Line;
    } else if (k == "undercurl") {
      h.underline = Underline::Curl;
    } else if (k == "underdouble") {
      h.underline = Underline::Double;
    } else if (k == "underdotted") {
      h.underline = Underline::Dotted;
    } else if (k == "underdashed") {
      h.underline = Underline::Dashed;
    } else if (k == "blend") {
      h.blend = bounded_arg(value, 0, 100);
    }
  }
  return h;
}

// One `[text, hl_id, repeat]` cell of grid_line, `repeat` cells wide.
struct CellRun {
  std::string_view text;
  int hl;
  int repeat;
};

// grid_line's cells, which must fit the `columns` left in the row. A cell
// without an hl_id reuses the previous one; the first cell always has one.
std::vector<CellRun> parse_cells(const UiState& s, const msgpack::Array& cells, int columns) {
  std::vector<CellRun> runs;
  std::optional<int> hl;
  for (const msgpack::Value& cell : cells) {
    const msgpack::Array& c = cell.as_array();
    if (c.size() >= 2) {
      hl = hl_arg(s, c[1]);
    } else if (!hl) {
      throw msgpack::TypeError("grid_line's first cell has no hl_id");
    }
    const int repeat = c.size() >= 3 ? count_arg(c[2]) : 1;
    columns -= repeat;
    if (columns < 0) {
      throw msgpack::TypeError(std::format("grid_line runs {} cells past the end of its row", -columns));
    }
    runs.push_back({c.at(0).as_string(), *hl, repeat});
  }
  return runs;
}

void grid_line(UiState& s, const msgpack::Array& a) {
  Grid& grid = grid_arg(s, a.at(0));
  const int row = index_arg(a.at(1), grid.height());
  int col = index_arg(a.at(2), grid.width());
  for (const CellRun& run : parse_cells(s, a.at(3).as_array(), grid.width() - col)) {
    col = grid.put(row, col, run.text, run.hl, run.repeat);
  }
}

void grid_scroll(UiState& s, const msgpack::Array& a) {
  Grid& grid = grid_arg(s, a.at(0));
  const int top = index_arg(a.at(1), grid.height());
  const int bot = bounded_arg(a.at(2), top + 1, grid.height());
  const int left = index_arg(a.at(3), grid.width());
  const int right = bounded_arg(a.at(4), left + 1, grid.width());
  const int rows = bounded_arg(a.at(5), top - bot, bot - top);
  if (rows == 0) {
    throw msgpack::TypeError("grid_scroll by 0 rows");
  }
  grid.scroll(top, bot, left, right, rows);
}

} // namespace

void register_grid_events(RedrawDispatcher& d) {
  d.add("default_colors_set", [](UiState& s, const msgpack::Array& a) {
    s.highlights.set_defaults({rgb_arg(a.at(0)), rgb_arg(a.at(1)), rgb_arg(a.at(2))});
  });
  d.add("hl_attr_define", [](UiState& s, const msgpack::Array& a) {
    s.highlights.define(bounded_arg(a.at(0), 1, kMaxHlId), parse_rgb_attr(a.at(1)));
  });
  d.add("hl_group_set", [](UiState& s, const msgpack::Array& a) { s.highlights.set_group(a.at(0).as_string(), hl_arg(s, a.at(1))); });
  d.add("grid_resize", [](UiState& s, const msgpack::Array& a) {
    const int id = int_arg(a.at(0));
    const int width = bounded_arg(a.at(1), 0, kMaxGridSide);
    const int height = bounded_arg(a.at(2), 0, kMaxGridSide);
    s.grids[id].resize(width, height);
  });
  d.add("grid_clear", [](UiState& s, const msgpack::Array& a) { grid_arg(s, a.at(0)).clear(); });
  d.add("grid_destroy", [](UiState& s, const msgpack::Array& a) {
    const int id = int_arg(a.at(0));
    s.grids.erase(id);
    s.placements.erase(id);
  });
  d.add("grid_line", grid_line);
  d.add("grid_cursor_goto", [](UiState& s, const msgpack::Array& a) {
    const Grid& grid = grid_arg(s, a.at(0));
    s.cursor = {int_arg(a.at(0)), index_arg(a.at(1), grid.height()), index_arg(a.at(2), grid.width())};
  });
  d.add("grid_scroll", grid_scroll);
}

} // namespace sailfish::ui
