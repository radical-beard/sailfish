#include "render/shape_cache.h"

#include <hb.h>

#include <cstring>

namespace sailfish::render {

namespace {

constexpr uint64_t kEvictAfterFrames = 600;
constexpr size_t kEvictionThreshold = 4096;

} // namespace

ShapeCache::ShapeCache() : buffer_(hb_buffer_create()) {}

ShapeCache::~ShapeCache() { hb_buffer_destroy(buffer_); }

const std::vector<ShapedGlyph>& ShapeCache::shape(const FontFace& face, std::string_view text) {
  const FontFace* face_ptr = &face;
  key_.assign(reinterpret_cast<const char*>(&face_ptr), sizeof(face_ptr));
  key_.append(text);
  Entry& entry = entries_[key_];
  if (entry.last_used == 0) {
    hb_buffer_reset(buffer_);
    hb_buffer_add_utf8(buffer_, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer_);
    hb_shape(face.shaper(), buffer_, nullptr, 0);
    unsigned count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer_, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer_, &count);
    entry.glyphs.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
      entry.glyphs.push_back(
        {infos[i].codepoint, infos[i].cluster, positions[i].x_offset / 64.0f, positions[i].y_offset / 64.0f, positions[i].x_advance / 64.0f});
    }
  }
  entry.last_used = frame_ + 1;
  return entry.glyphs;
}

void ShapeCache::end_frame() {
  ++frame_;
  if (entries_.size() > kEvictionThreshold) {
    std::erase_if(entries_, [&](const auto& kv) { return kv.second.last_used + kEvictAfterFrames < frame_; });
  }
}

} // namespace sailfish::render
