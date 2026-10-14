#pragma once
#include <cstdint>
#include <vector>

#include "platform/font_locator.h"
#include "render/font_spec.h"

typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;
typedef struct hb_font_t hb_font_t;

namespace sailfish::render {

// Owns the FreeType library instance shared by every face.
class FontLibrary {
public:
  FontLibrary();
  ~FontLibrary();
  FontLibrary(const FontLibrary&) = delete;
  FontLibrary& operator=(const FontLibrary&) = delete;
  [[nodiscard]] FT_Library get() const { return library_; }

private:
  FT_Library library_ = nullptr;
};

// A rasterized glyph: straight-alpha RGBA. `left`/`top` place the bitmap
// relative to the pen position on the baseline (top is upward).
struct GlyphBitmap {
  int width = 0;
  int height = 0;
  int left = 0;
  int top = 0;
  bool color = false;
  std::vector<uint8_t> rgba;
};

// Vertical metrics in pixels; y grows upward from the baseline.
struct FaceMetrics {
  double ascender = 0;
  double descender = 0;  // negative
  double advance = 0;    // width of '0', i.e. one monospace cell
  double underline_position = 0;
  double underline_thickness = 1;
  double strikeout_position = 0;
};

struct RasterOptions {
  Hinting hinting = Hinting::Light;
  bool antialias = true;
  bool synthetic_bold = false;    // the family has no real bold face
  bool synthetic_italic = false;  // ... or no real italic face
};

// One font face at one pixel size: FreeType rasterizes, HarfBuzz shapes.
class FontFace {
public:
  FontFace(FT_Library library, const platform::FontFile& file, double pixel_size, RasterOptions options);
  ~FontFace();
  FontFace(const FontFace&) = delete;
  FontFace& operator=(const FontFace&) = delete;

  [[nodiscard]] uint32_t glyph_for(char32_t codepoint) const;  // 0: not in this face
  [[nodiscard]] hb_font_t* shaper() const { return hb_font_; }
  [[nodiscard]] const FaceMetrics& metrics() const { return metrics_; }
  [[nodiscard]] bool has_color() const { return has_color_; }
  [[nodiscard]] const platform::FontFile& file() const { return file_; }

  // Bitmap-only faces (color emoji) are scaled to `fit_height` pixels.
  GlyphBitmap rasterize(uint32_t glyph, int fit_height) const;

private:
  platform::FontFile file_;
  FT_Face face_ = nullptr;
  hb_font_t* hb_font_ = nullptr;
  FaceMetrics metrics_;
  RasterOptions options_;
  int32_t load_flags_ = 0;
  bool has_color_ = false;
  bool scalable_ = true;
};

} // namespace sailfish::render
