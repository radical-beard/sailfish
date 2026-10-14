#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "render/layer.h"

namespace sailfish::render {

// Neovim's grids composited like the TUI would: the root grid, then split
// windows, then floats in Neovim's composition order.
// What a terminal can't do: floats cast a soft shadow, and windows scroll
// smoothly, sliding from the old view to the new one.
class GridLayer final : public Layer {
public:
  void draw(Frame& frame) override;
  [[nodiscard]] std::optional<double> next_frame_in(const Frame& frame) const override;

  void reset() override { scrolls_.clear(); }

private:
  struct Scroll {
    uint64_t seq = 0;    // the viewport move this animates
    double start = 0;
    double rows = 0;     // distance to travel from `start`; positive: content moving up
    std::vector<std::vector<ui::Cell>> old_rows;  // rows pushed out, in screen order
  };

  static void start_scroll(Scroll& s, const ui::Viewport& v, double now, int viewport_rows);
  void draw_window(Frame& f, int id, const ui::Placement& p);

  std::unordered_map<int, Scroll> scrolls_;
  bool animating_ = false;
};

} // namespace sailfish::render
