#pragma once
#include "render/font_set.h"
#include "render/glyph_cache.h"
#include "render/quad_batch.h"
#include "render/shape_cache.h"
#include "ui/grid.h"
#include "ui/highlight.h"

namespace sailfish::render {

// Draws Neovim grids cell-aligned: merged background runs, shaped text runs
// (so ligatures form), then underline/undercurl/strikethrough decorations.
class GridPainter {
public:
  GridPainter(FontSet& fonts, GlyphCache& glyphs, ShapeCache& shapes, QuadBatch& batch)
    : fonts_(fonts), glyphs_(glyphs), shapes_(shapes), batch_(batch) {}

  // Paints the top-left `rows` x `cols` cells of `grid` at pixel `origin`.
  void paint(const ui::Grid& grid, const ui::HighlightTable& hl, SDL_FPoint origin, int rows, int cols);
  // Paints rows [first_row, end_row) where they sit relative to `origin`.
  void paint_rows(const ui::Grid& grid, const ui::HighlightTable& hl, SDL_FPoint origin, int first_row, int end_row, int cols);
  // Draws one cell's text in `fg` (the cursor redraws the character under a block).
  void paint_cell_text(const ui::Grid& grid, const ui::HighlightTable& hl, int row, int col, SDL_FPoint origin, ui::Rgb fg);

private:
  void paint_backgrounds(const ui::Grid& grid, const ui::HighlightTable& hl, int row, SDL_FPoint origin, int cols);
  void paint_text(const ui::Grid& grid, const ui::HighlightTable& hl, int row, SDL_FPoint origin, int cols);
  void paint_decorations(const ui::Grid& grid, const ui::HighlightTable& hl, int row, SDL_FPoint origin, int cols);
  void draw_run(const ui::Grid& grid, int row, int first_col, int end_col, const ui::HlAttr& attr, SDL_FPoint origin, const ui::Rgb* fg_override,
    const ui::HighlightTable& hl);

  FontSet& fonts_;
  GlyphCache& glyphs_;
  ShapeCache& shapes_;
  QuadBatch& batch_;
};

} // namespace sailfish::render
