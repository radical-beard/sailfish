#include "render/grid_painter.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>

namespace sailfish::render {

namespace {

constexpr SDL_FColor kWhite{1, 1, 1, 1};

bool is_blank(const ui::Cell& c) { return c.text == " "; }
bool is_wide_continuation(const ui::Cell& c) { return c.text.empty(); }

platform::FontStyle style_of(const ui::HlAttr& a) { return {a.bold, a.italic}; }

} // namespace

void GridPainter::paint(const ui::Grid& grid, const ui::HighlightTable& hl, SDL_FPoint origin, int rows, int cols) {
  paint_rows(grid, hl, origin, 0, rows, cols);
}

void GridPainter::paint_rows(const ui::Grid& grid, const ui::HighlightTable& hl, SDL_FPoint origin, int first_row, int end_row, int cols) {
  end_row = std::min(end_row, grid.height());
  cols = std::min(cols, grid.width());
  // All backgrounds first: descenders and italic overhang may cross into the
  // next row and must not be painted over by it.
  for (int r = first_row; r < end_row; ++r) {
    paint_backgrounds(grid, hl, r, origin, cols);
  }
  for (int r = first_row; r < end_row; ++r) {
    paint_text(grid, hl, r, origin, cols);
    paint_decorations(grid, hl, r, origin, cols);
  }
}

void GridPainter::paint_backgrounds(const ui::Grid& grid, const ui::HighlightTable& hl, int row, SDL_FPoint origin, int cols) {
  const CellMetrics& cell = fonts_.cell();
  const float y = origin.y + static_cast<float>(row * cell.height);
  for (int col = 0; col < cols;) {
    const int id = grid.at(row, col).hl;
    const ui::Rgb bg = hl.colors(id).bg;
    const int blend = hl.attr(id).blend;
    int end = col + 1;
    while (end < cols) {
      const int next = grid.at(row, end).hl;
      if (next != id && (hl.colors(next).bg != bg || hl.attr(next).blend != blend)) {
        break;
      }
      ++end;
    }
    batch_.fill({origin.x + static_cast<float>(col * cell.width), y, static_cast<float>((end - col) * cell.width), static_cast<float>(cell.height)},
      to_color(bg, 1.0f - static_cast<float>(blend) / 100.0f));
    col = end;
  }
}

void GridPainter::paint_text(const ui::Grid& grid, const ui::HighlightTable& hl, int row, SDL_FPoint origin, int cols) {
  for (int col = 0; col < cols;) {
    const ui::Cell& first = grid.at(row, col);
    if (is_blank(first) || is_wide_continuation(first)) {
      ++col;
      continue;
    }
    const ui::HlAttr& attr = hl.attr(first.hl);
    const FontFace& face = fonts_.face_for(first.text, style_of(attr));
    // A run shares highlight and face so it can shape as one string.
    int end = col + 1;
    while (end < cols) {
      const ui::Cell& next = grid.at(row, end);
      if (!is_wide_continuation(next) && (is_blank(next) || next.hl != first.hl || &fonts_.face_for(next.text, style_of(attr)) != &face)) {
        break;
      }
      ++end;
    }
    draw_run(grid, row, col, end, attr, origin, nullptr, hl);
    col = end;
  }
}

void GridPainter::paint_cell_text(const ui::Grid& grid, const ui::HighlightTable& hl, int row, int col, SDL_FPoint origin, ui::Rgb fg) {
  const ui::Cell& cell = grid.at(row, col);
  if (is_blank(cell) || is_wide_continuation(cell)) {
    return;
  }
  const int end = col + 1 < grid.width() && is_wide_continuation(grid.at(row, col + 1)) ? col + 2 : col + 1;
  draw_run(grid, row, col, end, hl.attr(cell.hl), origin, &fg, hl);
}

void GridPainter::draw_run(const ui::Grid& grid, int row, int first_col, int end_col, const ui::HlAttr& attr, SDL_FPoint origin,
  const ui::Rgb* fg_override, const ui::HighlightTable& hl) {
  const CellMetrics& cell = fonts_.cell();
  std::string text;
  std::vector<std::pair<uint32_t, int>> starts;  // byte offset in `text` -> column
  for (int c = first_col; c < end_col; ++c) {
    const ui::Cell& gc = grid.at(row, c);
    if (!is_wide_continuation(gc)) {
      starts.emplace_back(static_cast<uint32_t>(text.size()), c);
      text += gc.text;
    }
  }
  const FontFace& face = fonts_.face_for(grid.at(row, first_col).text, style_of(attr));
  const ui::Rgb fg = fg_override ? *fg_override : hl.colors(grid.at(row, first_col).hl).fg;
  const float row_y = origin.y + static_cast<float>(row * cell.height);

  // HarfBuzz positions a cluster's later glyphs (combining marks) relative
  // to the pen after the earlier ones, so the pen advances within a cluster.
  std::optional<uint32_t> cluster;
  float pen = 0;
  for (const ShapedGlyph& g : shapes_.shape(face, text)) {
    if (g.cluster != cluster) {
      cluster = g.cluster;
      pen = 0;
    }
    const float glyph_pen = pen;
    pen += g.x_advance;
    const auto at = std::upper_bound(starts.begin(), starts.end(), g.cluster, [](uint32_t v, const auto& s) { return v < s.first; });
    const int col = std::prev(at)->second;
    const CachedGlyph* cg = glyphs_.get(face, g.glyph, cell.height);
    if (!cg) {
      continue;
    }
    const float cell_x = origin.x + static_cast<float>(col * cell.width) + glyph_pen;
    SDL_FRect dst{0, 0, static_cast<float>(cg->width), static_cast<float>(cg->height)};
    if (cg->color) {
      // Color glyphs (emoji) are centered in their one or two cells.
      const bool wide = col + 1 < grid.width() && is_wide_continuation(grid.at(row, col + 1));
      const float span = static_cast<float>((wide ? 2 : 1) * cell.width);
      dst.x = cell_x + std::floor((span - dst.w) / 2);
      dst.y = row_y + std::floor((static_cast<float>(cell.height) - dst.h) / 2);
    } else {
      dst.x = std::round(cell_x + g.x_offset + static_cast<float>(cg->left));
      dst.y = std::round(row_y + static_cast<float>(cell.baseline) - g.y_offset - static_cast<float>(cg->top));
    }
    batch_.image(dst, cg->src, cg->color ? kWhite : to_color(fg));
  }
}

void GridPainter::paint_decorations(const ui::Grid& grid, const ui::HighlightTable& hl, int row, SDL_FPoint origin, int cols) {
  const CellMetrics& cell = fonts_.cell();
  const float t = static_cast<float>(cell.underline_thickness);
  const float row_y = origin.y + static_cast<float>(row * cell.height);
  for (int col = 0; col < cols;) {
    const int id = grid.at(row, col).hl;
    int end = col + 1;
    while (end < cols && grid.at(row, end).hl == id) {
      ++end;
    }
    const ui::HlAttr& attr = hl.attr(id);
    if (attr.underline != ui::Underline::None || attr.strikethrough || attr.overline) {
      const ui::Rgb fg = hl.colors(id).fg;
      // Underlines use the special color when one is set, like Neovim's TUI.
      const SDL_FColor line = to_color(attr.sp.value_or(fg));
      const float x0 = origin.x + static_cast<float>(col * cell.width);
      const float w = static_cast<float>((end - col) * cell.width);
      const float uy = row_y + static_cast<float>(cell.underline_y);
      switch (attr.underline) {
        case ui::Underline::None:
          break;
        case ui::Underline::Line:
          batch_.fill({x0, uy, w, t}, line);
          break;
        case ui::Underline::Double:
          batch_.fill({x0, uy, w, t}, line);
          batch_.fill({x0, uy - 2 * t, w, t}, line);
          break;
        case ui::Underline::Dotted:
          for (float x = 0; x < w; x += 2 * t) {
            batch_.fill({x0 + x, uy, t, t}, line);
          }
          break;
        case ui::Underline::Dashed:
          for (float x = 0; x < w; x += 6 * t) {
            batch_.fill({x0 + x, uy, std::min(4 * t, w - x), t}, line);
          }
          break;
        case ui::Underline::Curl: {
          const float amplitude = std::max(1.0f, t);
          const float period = static_cast<float>(cell.width);
          const float base = std::min(uy, row_y + static_cast<float>(cell.height) - 2 * amplitude - t);
          for (float x = 0; x < w; x += 1) {
            const float dy = std::round(amplitude * (1 + std::sin(2 * std::numbers::pi_v<float> * (x0 + x) / period)));
            batch_.fill({x0 + x, base + dy, 1, t}, line);
          }
          break;
        }
      }
      if (attr.strikethrough) {
        batch_.fill({x0, row_y + static_cast<float>(cell.strikeout_y), w, t}, to_color(fg));
      }
      if (attr.overline) {
        batch_.fill({x0, row_y, w, t}, to_color(fg));
      }
    }
    col = end;
  }
}

} // namespace sailfish::render
