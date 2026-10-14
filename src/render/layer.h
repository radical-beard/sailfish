#pragma once
#include <SDL3/SDL.h>

#include <optional>
#include <unordered_map>

#include "render/font_set.h"
#include "render/grid_painter.h"
#include "render/quad_batch.h"
#include "ui/ui_state.h"

namespace sailfish::render {

// Everything a layer may draw with during one frame.
struct Frame {
  QuadBatch& batch;
  GridPainter& painter;
  FontSet& fonts;
  const ui::UiState& state;
  SDL_FPoint size;  // drawable size in device pixels
  double now;       // seconds, monotonic
  bool focused;
  bool native_messages;  // the host draws the cmdline and messages (no plugin took them over)
  // Vertical pixel offset of each grid's content this frame (smooth
  // scrolling); written by the grid layer, read by grid_origin().
  std::unordered_map<int, float>& scroll_offsets;

  [[nodiscard]] const CellMetrics& cell() const { return fonts.cell(); }
  // Window width in cells.
  [[nodiscard]] int columns() const { return static_cast<int>(size.x) / cell().width; }
  // The command bar is the strip below Neovim's root grid.
  [[nodiscard]] float bar_top() const;
  // A scratch grid for widget text, every cell blank in `background_hl`.
  [[nodiscard]] static ui::Grid blank_grid(int cols, int rows, int background_hl);
  // Attribute id of a builtin highlight group (0, the default, if unset).
  [[nodiscard]] int group(std::string_view name) const { return state.highlights.group(name).value_or(0); }
  // Top-left pixel of a grid as composited, or nullopt if it isn't shown.
  [[nodiscard]] std::optional<SDL_FPoint> grid_origin(int grid) const;
  // A soft drop shadow under a floating rectangle (floats, menus).
  void shadow(SDL_FRect r);
};

// One stratum of the window, drawn in registration order (grids first,
// overlays last). New native UI registers a layer; the compositor never changes.
class Layer {
public:
  virtual ~Layer() = default;
  virtual void draw(Frame& frame) = 0;
  // Seconds until this layer needs another frame (animation, blink); nullopt when idle.
  [[nodiscard]] virtual std::optional<double> next_frame_in(const Frame&) const { return std::nullopt; }
  // A key was pressed (Neovim receives it too).
  virtual void on_key() {}
  // The UI model being drawn was replaced by another (a hot reload swapped
  // editors): forget anything remembered about the old one.
  virtual void reset() {}
};

} // namespace sailfish::render
