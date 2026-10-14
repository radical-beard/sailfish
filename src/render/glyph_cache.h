#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "render/font_face.h"

namespace sailfish::render {

struct CachedGlyph {
  SDL_FRect src;  // atlas pixels
  int width = 0;
  int height = 0;
  int left = 0;
  int top = 0;
  bool color = false;
};

// Rasterized glyphs packed into one RGBA atlas texture. A small white block
// is reserved so solid rectangles can sample the same texture and the whole
// frame batches into few draw calls. When the atlas fills up, the rest of the
// frame drops new glyphs and the next frame starts from an empty atlas.
class GlyphCache {
public:
  GlyphCache(SDL_Renderer* renderer, int atlas_size);
  ~GlyphCache();
  GlyphCache(const GlyphCache&) = delete;
  GlyphCache& operator=(const GlyphCache&) = delete;

  // nullptr for glyphs without pixels (spaces) or when the atlas is full.
  const CachedGlyph* get(const FontFace& face, uint32_t glyph, int fit_height);
  void clear();
  void begin_frame();
  // A glyph didn't fit this frame (and was dropped).
  [[nodiscard]] bool overflowed() const { return overflowed_; }

  [[nodiscard]] SDL_Texture* texture() const { return texture_; }
  [[nodiscard]] SDL_FRect white_texel() const { return {1, 1, 1, 1}; }
  [[nodiscard]] int atlas_size() const { return size_; }

private:
  struct Key {
    const FontFace* face;
    uint32_t glyph;
    int fit_height;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const {
      return std::hash<const void*>()(k.face) ^ (static_cast<size_t>(k.glyph) * 0x9e3779b97f4a7c15ull) ^ static_cast<size_t>(k.fit_height);
    }
  };
  struct Shelf {
    int y;
    int height;
    int x;
  };

  bool allocate(int w, int h, SDL_Rect& out);
  void reset_packing();

  SDL_Texture* texture_ = nullptr;
  int size_;
  std::vector<Shelf> shelves_;
  int next_shelf_y_ = 0;
  std::unordered_map<Key, CachedGlyph, KeyHash> glyphs_;
  std::unordered_map<Key, bool, KeyHash> empty_;  // glyphs known to have no pixels
  bool overflowed_ = false;
};

} // namespace sailfish::render
