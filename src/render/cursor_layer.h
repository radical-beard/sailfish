#pragma once
#include <string>

#include "render/layer.h"

namespace sailfish::render {

// The rectangle a `mode` cursor covers on a cell whose top-left is `at`.
SDL_FRect cursor_rect(const ui::ModeInfo& mode, SDL_FPoint at, const CellMetrics& cell, bool wide);

// Draws a cursor occupying `rect` over (row, col) of `grid`: filled in the
// mode's color (or the cell's inverted colors), redrawing the character under
// a settled block cursor; an unfocused window gets a hollow outline.
void paint_cursor(Frame& f, const ui::ModeInfo& mode, const ui::Grid& grid, int row, int col, SDL_FPoint grid_origin, SDL_FRect rect,
  bool redraw_character);

// The editor cursor: shape and color from 'guicursor', blinking, and a short
// glide between positions. Hidden while Neovim is busy or the external
// cmdline owns the cursor.
class CursorLayer final : public Layer {
public:
  void draw(Frame& frame) override;
  [[nodiscard]] std::optional<double> next_frame_in(const Frame& frame) const override;

private:
  struct Target {
    SDL_FRect rect{};
    int grid = 0;
    int row = 0;
    int col = 0;
    bool operator==(const Target& o) const {
      return grid == o.grid && row == o.row && col == o.col && rect.x == o.rect.x && rect.y == o.rect.y && rect.w == o.rect.w && rect.h == o.rect.h;
    }
  };

  [[nodiscard]] bool blink_visible(const ui::ModeInfo& mode, double now) const;

  Target target_;
  SDL_FRect current_{};
  bool placed_ = false;
  bool animating_ = false;
  double last_draw_ = 0;
  double blink_epoch_ = 0;
  std::string mode_;
};

} // namespace sailfish::render
