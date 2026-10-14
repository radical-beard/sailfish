#include "render/cmdline_layer.h"

#include <algorithm>

#include "render/cursor_layer.h"
#include "render/text_layout.h"

namespace sailfish::render {

CmdlineLayout layout_cmdline(const ui::CmdlineLevel& level, int columns) {
  CmdlineLayout layout;
  if (!level.firstc.empty()) {
    layout.line.push_back({0, level.firstc});
  }
  if (!level.prompt.empty()) {
    layout.line.push_back({0, level.prompt});
  }
  if (level.indent > 0) {
    layout.line.push_back({0, std::string(static_cast<size_t>(level.indent), ' ')});
  }
  layout.prefix_width = text_width(level.firstc) + text_width(level.prompt) + level.indent;

  std::string content;
  for (const auto& chunk : level.content) {
    layout.line.push_back(chunk);
    content += chunk.text;
  }
  const size_t pos = std::min(static_cast<size_t>(std::max(level.pos, 0)), content.size());
  layout.cursor_col = layout.prefix_width + text_width(std::string_view(content).substr(0, pos));
  layout.scroll = std::max(0, layout.cursor_col - columns + 2);
  return layout;
}

void CmdlineLayer::draw(Frame& f) {
  const ui::UiState& s = f.state;
  if (!f.native_messages || !s.cmdline.visible()) {
    return;
  }
  const CellMetrics& cell = f.cell();
  const int cols = f.columns();
  const int background = f.group("MsgArea");
  const float bar_y = f.bar_top();

  const int block_rows = static_cast<int>(s.cmdline.block.size());
  if (block_rows > 0) {
    ui::Grid block = Frame::blank_grid(cols, block_rows, background);
    for (int r = 0; r < block_rows; ++r) {
      put_line(block, r, 0, s.cmdline.block[static_cast<size_t>(r)]);
    }
    f.painter.paint(block, s.highlights, {0, bar_y - static_cast<float>(block_rows * cell.height)}, block_rows, cols);
  }

  const ui::CmdlineLevel& level = s.cmdline.levels.back();
  const CmdlineLayout layout = layout_cmdline(level, cols);
  const int width = layout.scroll + cols;
  ui::Grid bar = Frame::blank_grid(width, 1, background);
  put_line(bar, 0, 0, layout.line);
  if (!level.special_char.empty() && layout.cursor_col < width) {
    bar.put(0, layout.cursor_col, level.special_char, background, 1);
  }
  const SDL_FPoint origin{-static_cast<float>(layout.scroll * cell.width), bar_y};
  f.painter.paint(bar, s.highlights, origin, 1, width);

  if (const ui::ModeInfo* mode = s.current_mode(); mode && layout.cursor_col < width) {
    const SDL_FPoint at{origin.x + static_cast<float>(layout.cursor_col * cell.width), bar_y};
    paint_cursor(f, *mode, bar, 0, layout.cursor_col, origin, cursor_rect(*mode, at, cell, false), true);
  }
}

} // namespace sailfish::render
