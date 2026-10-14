#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "render/font_face.h"

typedef struct hb_buffer_t hb_buffer_t;

namespace sailfish::render {

struct ShapedGlyph {
  uint32_t glyph;
  uint32_t cluster;  // byte offset of the source text the glyph came from
  float x_offset;    // pixels
  float y_offset;
  // Pixels. Clusters are laid out on the cell grid, so advances only matter
  // between glyphs of one cluster (a base character and its marks).
  float x_advance;
};

// Shapes runs with HarfBuzz and remembers the results: editor text repeats
// the same identifiers constantly, so most runs are cache hits. Entries not
// used for a while are evicted at frame end.
class ShapeCache {
public:
  ShapeCache();
  ~ShapeCache();
  ShapeCache(const ShapeCache&) = delete;
  ShapeCache& operator=(const ShapeCache&) = delete;

  const std::vector<ShapedGlyph>& shape(const FontFace& face, std::string_view text);
  void end_frame();
  void clear() { entries_.clear(); }

private:
  struct Entry {
    std::vector<ShapedGlyph> glyphs;
    uint64_t last_used = 0;
  };

  hb_buffer_t* buffer_;
  std::unordered_map<std::string, Entry> entries_;
  std::string key_;
  uint64_t frame_ = 0;
};

} // namespace sailfish::render
