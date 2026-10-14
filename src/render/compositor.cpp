#include "render/compositor.h"

#include <algorithm>

namespace sailfish::render {

namespace {

constexpr int kAtlasSize = 4096;

} // namespace

std::optional<SDL_FPoint> Frame::grid_origin(int grid) const {
  if (grid == 1) {
    return SDL_FPoint{0, 0};
  }
  const auto it = state.placements.find(grid);
  if (it == state.placements.end() || !it->second.visible) {
    return std::nullopt;
  }
  const auto offset = scroll_offsets.find(grid);
  return SDL_FPoint{static_cast<float>(it->second.col * cell().width),
    static_cast<float>(it->second.row * cell().height) + (offset == scroll_offsets.end() ? 0.0f : offset->second)};
}

Compositor::Compositor(SDL_Renderer* renderer, platform::FontLocator& locator, std::vector<platform::FontFile> bundled_fonts, const FontRequest& font)
  : renderer_(renderer), locator_(locator), bundled_fonts_(std::move(bundled_fonts)), glyphs_(renderer, kAtlasSize), batch_(renderer, glyphs_) {
  set_font(font);
}

void Compositor::set_font(const FontRequest& request) {
  if (font_request_ == request) {
    return;
  }
  fonts_ = std::make_unique<FontSet>(library_, locator_, request.spec, request.pixel_size, request.linespace, bundled_fonts_);
  font_request_ = request;
  glyphs_.clear();
  shapes_.clear();
}

void Frame::shadow(SDL_FRect r) {
  // Concentric translucent rings fake a blurred shadow cheaply.
  constexpr int kRings = 6;
  const float step = static_cast<float>(cell().height) / 12.0f;
  for (int i = kRings; i >= 1; --i) {
    const float grow = step * static_cast<float>(i);
    batch.fill({r.x - grow * 0.5f, r.y + step, r.w + grow, r.h + grow}, {0, 0, 0, 0.045f});
  }
}

float Frame::bar_top() const {
  const auto root = state.grids.find(1);
  return root == state.grids.end() ? 0.0f : static_cast<float>(root->second.height() * cell().height);
}

ui::Grid Frame::blank_grid(int cols, int rows, int background_hl) {
  ui::Grid g;
  g.resize(cols, rows);
  for (int r = 0; r < rows; ++r) {
    g.put(r, 0, " ", background_hl, cols);
  }
  return g;
}

void Compositor::key_pressed() {
  for (const auto& layer : layers_) {
    layer->on_key();
  }
}

void Compositor::reset() {
  for (const auto& layer : layers_) {
    layer->reset();
  }
}

std::optional<double> Compositor::render(const ui::UiState& state, const FrameOptions& options) {
  glyphs_.begin_frame();
  int w = 0;
  int h = 0;
  SDL_GetRenderOutputSize(renderer_, &w, &h);

  const SDL_FColor bg = to_color(state.highlights.defaults().bg);
  SDL_SetRenderDrawColorFloat(renderer_, bg.r, bg.g, bg.b, 1.0f);
  SDL_RenderClear(renderer_);

  GridPainter painter(*fonts_, glyphs_, shapes_, batch_);
  std::unordered_map<int, float> scroll_offsets;
  Frame frame{batch_, painter, *fonts_, state, {static_cast<float>(w), static_cast<float>(h)}, options.now, options.focused, options.native_messages,
    scroll_offsets};
  std::optional<double> next;
  for (const auto& layer : layers_) {
    layer->draw(frame);
    if (const auto wait = layer->next_frame_in(frame)) {
      next = next ? std::min(*next, *wait) : *wait;
    }
  }
  batch_.flush();
  // Glyphs that didn't fit are missing from this frame; the next one starts
  // with an empty atlas and draws them.
  if (glyphs_.overflowed()) {
    next = 0;
  }
  if (!screenshot_path_.empty()) {
    if (SDL_Surface* pixels = SDL_RenderReadPixels(renderer_, nullptr)) {
      SDL_SaveBMP(pixels, screenshot_path_.c_str());
      SDL_DestroySurface(pixels);
    }
    screenshot_path_.clear();
  }
  SDL_RenderPresent(renderer_);
  shapes_.end_frame();
  return next;
}

} // namespace sailfish::render
