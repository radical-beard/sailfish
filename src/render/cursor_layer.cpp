#include "render/cursor_layer.h"

#include <algorithm>
#include <cmath>

namespace sailfish::render {

namespace {

constexpr double kGlideSeconds = 0.025;  // exponential time constant of the glide
constexpr double kAnimationFrame = 1.0 / 240.0;

bool blinks(const ui::ModeInfo& m) { return m.blinkwait > 0 && m.blinkon > 0 && m.blinkoff > 0; }

float approach(float from, float to, float k) { return std::abs(to - from) < 0.5f ? to : from + (to - from) * k; }

} // namespace

SDL_FRect cursor_rect(const ui::ModeInfo& mode, SDL_FPoint at, const CellMetrics& cell, bool wide) {
  SDL_FRect rect{at.x, at.y, static_cast<float>(cell.width * (wide ? 2 : 1)), static_cast<float>(cell.height)};
  if (mode.shape == ui::CursorShape::Vertical) {
    rect.w = std::max(1.0f, std::round(static_cast<float>(cell.width * mode.cell_percentage) / 100.0f));
  } else if (mode.shape == ui::CursorShape::Horizontal) {
    const float h = std::max(1.0f, std::round(static_cast<float>(cell.height * mode.cell_percentage) / 100.0f));
    rect.y += rect.h - h;
    rect.h = h;
  }
  return rect;
}

void paint_cursor(Frame& f, const ui::ModeInfo& mode, const ui::Grid& grid, int row, int col, SDL_FPoint grid_origin, SDL_FRect rect,
  bool redraw_character) {
  const ui::HighlightTable& hl = f.state.highlights;
  // attr_id 0 means "invert the cell"; otherwise the mode names a highlight.
  const ui::ResolvedColors under = hl.colors(grid.at(row, col).hl);
  const ui::ResolvedColors mode_colors = hl.colors(mode.attr_id);
  const ui::Rgb cursor_bg = mode.attr_id ? mode_colors.bg : under.fg;
  const ui::Rgb cursor_fg = mode.attr_id ? mode_colors.fg : under.bg;

  if (!f.focused) {
    const float t = std::max(1.0f, static_cast<float>(f.cell().height) / 16.0f);
    const SDL_FRect& r = rect;
    for (const SDL_FRect edge : {SDL_FRect{r.x, r.y, r.w, t}, SDL_FRect{r.x, r.y + r.h - t, r.w, t}, SDL_FRect{r.x, r.y, t, r.h}, SDL_FRect{r.x + r.w - t, r.y, t, r.h}}) {
      f.batch.fill(edge, to_color(cursor_bg));
    }
    return;
  }
  f.batch.fill(rect, to_color(cursor_bg));
  if (mode.shape == ui::CursorShape::Block && redraw_character) {
    f.painter.paint_cell_text(grid, hl, row, col, grid_origin, cursor_fg);
  }
}

bool CursorLayer::blink_visible(const ui::ModeInfo& mode, double now) const {
  if (!blinks(mode)) {
    return true;
  }
  double t = (now - blink_epoch_) * 1000.0 - mode.blinkwait;
  if (t < 0) {
    return true;
  }
  return std::fmod(t, mode.blinkon + mode.blinkoff) >= mode.blinkoff;
}

void CursorLayer::draw(Frame& f) {
  const ui::UiState& s = f.state;
  const ui::ModeInfo* mode = s.current_mode();
  const auto origin = f.grid_origin(s.cursor.grid);
  const auto grid_it = s.grids.find(s.cursor.grid);
  if (!mode || s.busy || s.cmdline.visible() || !origin || grid_it == s.grids.end()) {
    animating_ = false;
    return;
  }
  const ui::Grid& grid = grid_it->second;
  const int row = s.cursor.row;
  const int col = s.cursor.col;
  if (row < 0 || row >= grid.height() || col < 0 || col >= grid.width()) {
    animating_ = false;
    return;
  }

  const CellMetrics& cell = f.cell();
  const bool wide = col + 1 < grid.width() && grid.at(row, col + 1).text.empty();
  const SDL_FRect rect = cursor_rect(*mode, {origin->x + static_cast<float>(col * cell.width), origin->y + static_cast<float>(row * cell.height)}, cell, wide);

  const Target target{rect, s.cursor.grid, row, col};
  if (target != target_ || s.mode != mode_) {
    target_ = target;
    mode_ = s.mode;
    blink_epoch_ = f.now;
    if (!placed_) {
      current_ = rect;
      placed_ = true;
    }
  }
  const float k = static_cast<float>(1.0 - std::exp(-(f.now - last_draw_) / kGlideSeconds));
  last_draw_ = f.now;
  current_ = {approach(current_.x, rect.x, k), approach(current_.y, rect.y, k), approach(current_.w, rect.w, k), approach(current_.h, rect.h, k)};
  animating_ = current_.x != rect.x || current_.y != rect.y || current_.w != rect.w || current_.h != rect.h;

  if (blink_visible(*mode, f.now)) {
    paint_cursor(f, *mode, grid, row, col, *origin, current_, !animating_);
  }
}

std::optional<double> CursorLayer::next_frame_in(const Frame& f) const {
  if (animating_) {
    return kAnimationFrame;
  }
  const ui::ModeInfo* mode = f.state.current_mode();
  if (!mode || !blinks(*mode) || !placed_) {
    return std::nullopt;
  }
  const double t = (f.now - blink_epoch_) * 1000.0 - mode->blinkwait;
  if (t < 0) {
    return -t / 1000.0;
  }
  const double phase = std::fmod(t, mode->blinkon + mode->blinkoff);
  const double until = phase < mode->blinkoff ? mode->blinkoff - phase : mode->blinkon + mode->blinkoff - phase;
  return until / 1000.0;
}

} // namespace sailfish::render
