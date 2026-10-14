#include "render/glyph_cache.h"

#include <stdexcept>

#include "log/log.h"

namespace sailfish::render {

namespace {

constexpr int kPadding = 1;
constexpr int kWhiteBlock = 3;  // sampled at its center texel

} // namespace

GlyphCache::GlyphCache(SDL_Renderer* renderer, int atlas_size) : size_(atlas_size) {
  texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, size_, size_);
  if (!texture_) {
    throw std::runtime_error(std::string("cannot create glyph atlas: ") + SDL_GetError());
  }
  SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
  reset_packing();
}

GlyphCache::~GlyphCache() { SDL_DestroyTexture(texture_); }

void GlyphCache::reset_packing() {
  shelves_.clear();
  next_shelf_y_ = 0;
  SDL_Rect white;
  allocate(kWhiteBlock, kWhiteBlock, white);
  const std::vector<uint32_t> pixels(kWhiteBlock * kWhiteBlock, 0xffffffff);
  SDL_UpdateTexture(texture_, &white, pixels.data(), kWhiteBlock * 4);
}

void GlyphCache::clear() {
  glyphs_.clear();
  empty_.clear();
  reset_packing();
}

void GlyphCache::begin_frame() {
  if (overflowed_) {
    log::info("glyph atlas full; starting over");
    clear();
    overflowed_ = false;
  }
}

bool GlyphCache::allocate(int w, int h, SDL_Rect& out) {
  const int pw = w + kPadding;
  const int ph = h + kPadding;
  for (Shelf& shelf : shelves_) {
    // Reuse shelves that fit without wasting more than a third of their height.
    if (ph <= shelf.height && ph * 3 >= shelf.height * 2 && shelf.x + pw <= size_) {
      out = {shelf.x, shelf.y, w, h};
      shelf.x += pw;
      return true;
    }
  }
  if (next_shelf_y_ + ph > size_ || pw > size_) {
    return false;
  }
  shelves_.push_back({next_shelf_y_, ph, pw});
  out = {0, next_shelf_y_, w, h};
  next_shelf_y_ += ph;
  return true;
}

const CachedGlyph* GlyphCache::get(const FontFace& face, uint32_t glyph, int fit_height) {
  const Key key{&face, glyph, fit_height};
  if (const auto it = glyphs_.find(key); it != glyphs_.end()) {
    return &it->second;
  }
  if (overflowed_ || empty_.contains(key)) {
    return nullptr;
  }
  const GlyphBitmap bitmap = face.rasterize(glyph, fit_height);
  if (bitmap.width == 0 || bitmap.height == 0) {
    empty_[key] = true;
    return nullptr;
  }
  SDL_Rect slot;
  if (!allocate(bitmap.width, bitmap.height, slot)) {
    overflowed_ = true;
    return nullptr;
  }
  SDL_UpdateTexture(texture_, &slot, bitmap.rgba.data(), bitmap.width * 4);
  const CachedGlyph cached{
    {static_cast<float>(slot.x), static_cast<float>(slot.y), static_cast<float>(slot.w), static_cast<float>(slot.h)},
    bitmap.width, bitmap.height, bitmap.left, bitmap.top, bitmap.color};
  return &glyphs_.emplace(key, cached).first->second;
}

} // namespace sailfish::render
