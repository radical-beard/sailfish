#include "render/quad_batch.h"

namespace sailfish::render {

void QuadBatch::quad(SDL_FRect dst, SDL_FRect src, SDL_FColor color) {
  const float inv = 1.0f / static_cast<float>(glyphs_.atlas_size());
  const float u0 = src.x * inv;
  const float v0 = src.y * inv;
  const float u1 = (src.x + src.w) * inv;
  const float v1 = (src.y + src.h) * inv;
  const int base = static_cast<int>(vertices_.size());
  vertices_.push_back({{dst.x, dst.y}, color, {u0, v0}});
  vertices_.push_back({{dst.x + dst.w, dst.y}, color, {u1, v0}});
  vertices_.push_back({{dst.x + dst.w, dst.y + dst.h}, color, {u1, v1}});
  vertices_.push_back({{dst.x, dst.y + dst.h}, color, {u0, v1}});
  indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void QuadBatch::fill(SDL_FRect rect, SDL_FColor color) {
  if (rect.w > 0 && rect.h > 0) {
    quad(rect, glyphs_.white_texel(), color);
  }
}

void QuadBatch::image(SDL_FRect dst, SDL_FRect atlas_src, SDL_FColor tint) { quad(dst, atlas_src, tint); }

void QuadBatch::clip(const SDL_Rect* rect) {
  flush();
  SDL_SetRenderClipRect(renderer_, rect);
}

void QuadBatch::flush() {
  if (!indices_.empty()) {
    SDL_RenderGeometry(renderer_, glyphs_.texture(), vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(),
      static_cast<int>(indices_.size()));
  }
  vertices_.clear();
  indices_.clear();
}

} // namespace sailfish::render
