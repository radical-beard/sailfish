#pragma once
#include <SDL3/SDL.h>

#include <vector>

#include "render/glyph_cache.h"
#include "ui/highlight.h"

namespace sailfish::render {

inline SDL_FColor to_color(ui::Rgb rgb, float alpha = 1.0f) {
  return {((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, alpha};
}

// Accumulates textured quads against the glyph atlas and submits them in
// order with one SDL_RenderGeometry call per flush.
class QuadBatch {
public:
  QuadBatch(SDL_Renderer* renderer, GlyphCache& glyphs) : renderer_(renderer), glyphs_(glyphs) {}

  // Restricts drawing to `rect` (nullptr: the whole window). Flushes first,
  // since a clip applies per draw call.
  void clip(const SDL_Rect* rect);

  void fill(SDL_FRect rect, SDL_FColor color);
  void image(SDL_FRect dst, SDL_FRect atlas_src, SDL_FColor tint);
  void flush();

private:
  void quad(SDL_FRect dst, SDL_FRect src, SDL_FColor color);

  SDL_Renderer* renderer_;
  GlyphCache& glyphs_;
  std::vector<SDL_Vertex> vertices_;
  std::vector<int> indices_;
};

} // namespace sailfish::render
