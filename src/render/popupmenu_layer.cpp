#include "render/popupmenu_layer.h"

#include <algorithm>

#include "render/cmdline_layer.h"
#include "render/text_layout.h"

namespace sailfish::render {

namespace {

constexpr int kMaxRows = 15;

struct Columns {
  int word = 0;
  int kind = 0;
  int menu = 0;
};

Columns measure(const std::vector<ui::PopupmenuItem>& items) {
  Columns c;
  for (const auto& item : items) {
    c.word = std::max(c.word, text_width(item.word));
    c.kind = std::max(c.kind, text_width(item.kind));
    c.menu = std::max(c.menu, text_width(item.menu));
  }
  return c;
}

} // namespace

void PopupmenuLayer::draw(Frame& f) {
  const ui::UiState& s = f.state;
  const ui::PopupmenuState& pum = s.popupmenu;
  if (!pum.visible || pum.items.empty()) {
    return;
  }
  const CellMetrics& cell = f.cell();
  const int screen_cols = f.columns();
  const int screen_rows = static_cast<int>(f.bar_top()) / cell.height;

  // Anchor: the cell where the completed word starts.
  float anchor_x = 0;
  float anchor_y = 0;
  bool above = false;
  if (pum.grid == -1) {
    if (!s.cmdline.visible()) {
      return;
    }
    const CmdlineLayout layout = layout_cmdline(s.cmdline.levels.back(), screen_cols);
    std::string content;
    for (const auto& chunk : s.cmdline.levels.back().content) {
      content += chunk.text;
    }
    const size_t col = std::min(static_cast<size_t>(std::max(pum.col, 0)), content.size());
    anchor_x = static_cast<float>((layout.prefix_width + text_width(std::string_view(content).substr(0, col)) - layout.scroll) * cell.width);
    anchor_y = f.bar_top();
    above = true;
  } else {
    const auto origin = f.grid_origin(pum.grid);
    if (!origin) {
      return;
    }
    anchor_x = origin->x + static_cast<float>(pum.col * cell.width);
    anchor_y = origin->y + static_cast<float>((pum.row + 1) * cell.height);
  }

  const Columns c = measure(pum.items);
  const int kind_col = 1 + c.word + 1;
  const int menu_col = kind_col + (c.kind > 0 ? c.kind + 1 : 0);
  const bool scrollbar = static_cast<int>(pum.items.size()) > kMaxRows;
  const int width = std::min(screen_cols, menu_col + (c.menu > 0 ? c.menu + 1 : 0) + (scrollbar ? 1 : 0));

  const int anchor_row = static_cast<int>(anchor_y) / cell.height;
  const int room_below = screen_rows - anchor_row;
  const int room_above = anchor_row - (pum.grid == -1 ? 0 : 1);
  above = above || (room_below < std::min(static_cast<int>(pum.items.size()), kMaxRows) && room_above > room_below);
  const int rows = std::max(1, std::min({static_cast<int>(pum.items.size()), kMaxRows, above ? room_above : room_below}));

  // Keep the selection in view.
  const int count = static_cast<int>(pum.items.size());
  const int first = std::clamp(pum.selected - rows / 2, 0, std::max(0, count - rows));

  const int normal = f.group("Pmenu");
  const int selected = f.group("PmenuSel");
  ui::Grid menu = Frame::blank_grid(width, rows, normal);
  for (int r = 0; r < rows; ++r) {
    const int i = first + r;
    const auto& item = pum.items[static_cast<size_t>(i)];
    const bool is_selected = i == pum.selected;
    const int bg = is_selected ? selected : normal;
    menu.put(r, 0, " ", bg, width);
    put_text(menu, r, 1, item.word, bg);
    put_text(menu, r, kind_col, item.kind, is_selected ? f.group("PmenuKindSel") : f.group("PmenuKind"));
    put_text(menu, r, menu_col, item.menu, is_selected ? f.group("PmenuExtraSel") : f.group("PmenuExtra"));
    if (scrollbar) {
      const int thumb_start = first * rows / count;
      const int thumb_end = std::max(thumb_start + 1, (first + rows) * rows / count);
      menu.put(r, width - 1, " ", r >= thumb_start && r < thumb_end ? f.group("PmenuThumb") : f.group("PmenuSbar"), 1);
    }
  }

  float x = std::clamp(anchor_x - static_cast<float>(cell.width), 0.0f, static_cast<float>((screen_cols - width) * cell.width));
  float y = above ? anchor_y - static_cast<float>(rows * cell.height) - (pum.grid == -1 ? 0.0f : static_cast<float>(cell.height)) : anchor_y;
  x = std::max(0.0f, x);
  y = std::max(0.0f, y);
  f.shadow({x, y, static_cast<float>(width * cell.width), static_cast<float>(rows * cell.height)});
  f.painter.paint(menu, s.highlights, {x, y}, rows, width);
}

} // namespace sailfish::render
