#include "render/grid_layer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sailfish::render {

namespace {

constexpr double kScrollSeconds = 0.12;
constexpr double kAnimationFrame = 1.0 / 240.0;

// Cubic ease-out progress of an animation started at `start`, from 0 to 1.
double eased(double now, double start) {
  const double t = std::clamp((now - start) / kScrollSeconds, 0.0, 1.0);
  return 1.0 - std::pow(1.0 - t, 3.0);
}

ui::Grid grid_from_rows(const std::vector<std::vector<ui::Cell>>& rows, int width) {
  ui::Grid g;
  g.resize(width, static_cast<int>(rows.size()));
  for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
    const auto& row = rows[static_cast<size_t>(r)];
    for (int c = 0; c < std::min(width, static_cast<int>(row.size())); ++c) {
      g.put(r, c, row[static_cast<size_t>(c)].text, row[static_cast<size_t>(c)].hl, 1);
    }
  }
  return g;
}

} // namespace

// A move that arrives while an earlier one is still sliding continues from
// where that one has got to (holding j glides rather than jerking back),
// keeping the rows still on their way out beyond the newly pushed-out ones.
void GridLayer::start_scroll(Scroll& s, const ui::Viewport& v, double now, int viewport_rows) {
  const int delta = static_cast<int>(v.scroll_delta);
  const double left = s.rows * (1.0 - eased(now, s.start));
  const bool continues = left != 0 && (delta > 0) == (s.rows > 0);
  const double rows = delta + (continues ? left : 0.0);
  std::vector<std::vector<ui::Cell>> old_rows = v.scrolled_out;
  if (continues) {
    old_rows.insert(delta > 0 ? old_rows.begin() : old_rows.end(), s.old_rows.begin(), s.old_rows.end());
  }
  const bool animatable = !v.scrolled_out.empty() && std::abs(rows) < viewport_rows;
  s = {v.scroll_seq, now, animatable ? rows : 0.0, animatable ? std::move(old_rows) : std::vector<std::vector<ui::Cell>>{}};
}

void GridLayer::draw_window(Frame& f, int id, const ui::Placement& p) {
  const ui::Grid& g = f.state.grids.at(id);
  const ui::HighlightTable& hl = f.state.highlights;
  const CellMetrics& cell = f.cell();
  const SDL_FPoint origin{static_cast<float>(p.col * cell.width), static_cast<float>(p.row * cell.height)};
  const int top = std::clamp(p.margins.top, 0, p.height);
  const int bottom = std::clamp(p.height - p.margins.bottom, top, p.height);

  Scroll& scroll = scrolls_[id];
  if (p.viewport.scroll_seq != scroll.seq) {
    start_scroll(scroll, p.viewport, f.now, bottom - top);
  }
  const double progress = eased(f.now, scroll.start);
  if (scroll.rows == 0 || progress >= 1.0) {
    scroll.rows = 0;
    scroll.old_rows.clear();
    f.painter.paint(g, hl, origin, p.height, p.width);
    return;
  }

  animating_ = true;
  // At the start the new content sits exactly where its lines were before
  // the move, then slides into place.
  const float offset = static_cast<float>(std::round(scroll.rows * (1.0 - progress) * cell.height));
  f.scroll_offsets[id] = offset;

  // The winbar (and a float's border) stays put; only the viewport moves.
  f.painter.paint_rows(g, hl, origin, 0, top, p.width);
  f.painter.paint_rows(g, hl, origin, bottom, p.height, p.width);
  const SDL_Rect clip{static_cast<int>(origin.x), static_cast<int>(origin.y) + top * cell.height, p.width * cell.width, (bottom - top) * cell.height};
  f.batch.clip(&clip);
  f.painter.paint_rows(g, hl, {origin.x, origin.y + offset}, top, bottom, p.width);
  // The rows the move pushed out, beside the edge they left by.
  const ui::Grid old = grid_from_rows(scroll.old_rows, p.width);
  const float rows_px = static_cast<float>(old.height() * cell.height);
  const float old_y = scroll.rows > 0 ? origin.y + offset + static_cast<float>(top * cell.height) - rows_px
                                      : origin.y + offset + static_cast<float>(bottom * cell.height);
  f.painter.paint(old, hl, {origin.x, old_y}, old.height(), p.width);
  f.batch.clip(nullptr);
}

void GridLayer::draw(Frame& f) {
  animating_ = false;
  const ui::UiState& s = f.state;
  const CellMetrics& cell = f.cell();

  if (const auto root = s.grids.find(1); root != s.grids.end()) {
    f.painter.paint(root->second, s.highlights, {0, 0}, root->second.height(), root->second.width());
  }

  std::vector<std::pair<int, const ui::Placement*>> overlays;
  for (const auto& [id, p] : s.placements) {
    if (!p.visible || !s.grids.contains(id)) {
      continue;
    }
    if (p.role == ui::GridRole::Window) {
      draw_window(f, id, p);
    } else if (p.role == ui::GridRole::Float) {
      overlays.emplace_back(id, &p);
    }
  }
  std::stable_sort(overlays.begin(), overlays.end(), [](const auto& a, const auto& b) {
    return std::tie(a.second->zindex, a.second->compindex) < std::tie(b.second->zindex, b.second->compindex);
  });
  for (const auto& [id, p] : overlays) {
    const ui::Grid& g = s.grids.at(id);
    const SDL_FPoint origin{static_cast<float>(p->col * cell.width), static_cast<float>(p->row * cell.height)};
    const int cols = std::min(p->width > 0 ? p->width : g.width(), g.width());
    f.shadow({origin.x, origin.y, static_cast<float>(cols * cell.width), static_cast<float>(g.height() * cell.height)});
    f.painter.paint(g, s.highlights, origin, g.height(), cols);
  }
}

std::optional<double> GridLayer::next_frame_in(const Frame&) const {
  return animating_ ? std::optional<double>(kAnimationFrame) : std::nullopt;
}

} // namespace sailfish::render
