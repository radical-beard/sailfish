#include "render/font_face.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_DRIVER_H
#include FT_MODULE_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_TABLES_H
#include <hb.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <stdexcept>

namespace sailfish::render {

namespace {

// Blending happens in sRGB space, which renders FreeType's linear coverage
// visibly thin; a mild curve restores weight closer to native text.
constexpr double kCoverageGamma = 1.25;

const std::array<uint8_t, 256>& coverage_curve() {
  static const auto table = [] {
    std::array<uint8_t, 256> t{};
    for (int i = 0; i < 256; ++i) {
      t[static_cast<size_t>(i)] = static_cast<uint8_t>(std::lround(255.0 * std::pow(i / 255.0, 1.0 / kCoverageGamma)));
    }
    return t;
  }();
  return table;
}

double from_26_6(long v) { return static_cast<double>(v) / 64.0; }

FT_Long face_index_for(FT_Library library, const platform::FontFile& file) {
  FT_Face probe = nullptr;
  if (FT_New_Face(library, file.path.string().c_str(), -1, &probe) != 0) {
    throw std::runtime_error(std::format("cannot open font {}", file.path.string()));
  }
  const FT_Long count = probe->num_faces;
  FT_Done_Face(probe);
  if (count <= 1 || file.postscript_name.empty()) {
    return 0;
  }
  for (FT_Long i = 0; i < count; ++i) {
    FT_Face face = nullptr;
    if (FT_New_Face(library, file.path.string().c_str(), i, &face) != 0) {
      continue;
    }
    const char* name = FT_Get_Postscript_Name(face);
    const bool match = name && file.postscript_name == name;
    FT_Done_Face(face);
    if (match) {
      return i;
    }
  }
  return 0;
}

// Box-filter downscale for bitmap strikes (emoji are stored at up to 160px).
std::vector<uint8_t> downscale(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh) {
  std::vector<uint8_t> dst(static_cast<size_t>(dw * dh * 4));
  for (int y = 0; y < dh; ++y) {
    const int y0 = y * sh / dh;
    const int y1 = std::max(y0 + 1, (y + 1) * sh / dh);
    for (int x = 0; x < dw; ++x) {
      const int x0 = x * sw / dw;
      const int x1 = std::max(x0 + 1, (x + 1) * sw / dw);
      // Average in premultiplied space so transparent texels don't darken edges.
      double r = 0, g = 0, b = 0, a = 0;
      for (int sy = y0; sy < y1; ++sy) {
        for (int sx = x0; sx < x1; ++sx) {
          const uint8_t* p = &src[static_cast<size_t>((sy * sw + sx) * 4)];
          const double pa = p[3] / 255.0;
          r += p[0] * pa;
          g += p[1] * pa;
          b += p[2] * pa;
          a += p[3];
        }
      }
      const double n = static_cast<double>((y1 - y0) * (x1 - x0));
      uint8_t* q = &dst[static_cast<size_t>((y * dw + x) * 4)];
      const double alpha = a / n;
      const double unpremultiply = alpha > 0 ? 255.0 / alpha : 0;
      q[0] = static_cast<uint8_t>(std::min(255.0, r / n * unpremultiply));
      q[1] = static_cast<uint8_t>(std::min(255.0, g / n * unpremultiply));
      q[2] = static_cast<uint8_t>(std::min(255.0, b / n * unpremultiply));
      q[3] = static_cast<uint8_t>(alpha);
    }
  }
  return dst;
}

} // namespace

FontLibrary::FontLibrary() {
  if (FT_Init_FreeType(&library_) != 0) {
    throw std::runtime_error("FreeType failed to initialize");
  }
  FT_Bool no_darkening = 0;
  FT_Property_Set(library_, "autofitter", "no-stem-darkening", &no_darkening);
  FT_Property_Set(library_, "cff", "no-stem-darkening", &no_darkening);
}

FontLibrary::~FontLibrary() { FT_Done_FreeType(library_); }

FontFace::FontFace(FT_Library library, const platform::FontFile& file, double pixel_size, RasterOptions options)
  : file_(file), options_(options) {
  const FT_Long index = face_index_for(library, file);
  if (FT_New_Face(library, file.path.string().c_str(), index, &face_) != 0) {
    throw std::runtime_error(std::format("cannot open font {}", file.path.string()));
  }
  scalable_ = FT_IS_SCALABLE(face_);
  has_color_ = FT_HAS_COLOR(face_);

  if (scalable_) {
    FT_Set_Char_Size(face_, 0, static_cast<FT_F26Dot6>(std::lround(pixel_size * 64)), 72, 72);
  } else {
    // Choose the smallest strike at least as tall as the cell, else the largest.
    int best = 0;
    for (int i = 1; i < face_->num_fixed_sizes; ++i) {
      const double size = from_26_6(face_->available_sizes[i].y_ppem);
      const double best_size = from_26_6(face_->available_sizes[best].y_ppem);
      if ((best_size < pixel_size && size > best_size) || (size >= pixel_size && size < best_size)) {
        best = i;
      }
    }
    FT_Select_Size(face_, best);
  }

  load_flags_ = FT_LOAD_DEFAULT;
  if (has_color_) {
    load_flags_ |= FT_LOAD_COLOR;
  }
  if (!options_.antialias) {
    load_flags_ |= FT_LOAD_TARGET_MONO;
  } else if (options_.hinting == Hinting::None) {
    load_flags_ |= FT_LOAD_NO_HINTING;
  } else if (options_.hinting == Hinting::Light) {
    load_flags_ |= FT_LOAD_TARGET_LIGHT;
  }

  const FT_Size_Metrics& m = face_->size->metrics;
  const double scale = scalable_ ? 1.0 : pixel_size / from_26_6(m.y_ppem);
  metrics_.ascender = from_26_6(m.ascender) * scale;
  metrics_.descender = from_26_6(m.descender) * scale;
  metrics_.advance = from_26_6(m.max_advance) * scale;
  if (FT_Load_Char(face_, '0', load_flags_) == 0) {
    metrics_.advance = from_26_6(face_->glyph->advance.x) * scale;
  }
  if (scalable_) {
    metrics_.underline_position = from_26_6(FT_MulFix(face_->underline_position, m.y_scale));
    metrics_.underline_thickness = std::max(1.0, from_26_6(FT_MulFix(face_->underline_thickness, m.y_scale)));
    const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face_, FT_SFNT_OS2));
    metrics_.strikeout_position = os2 && os2->version != 0xffff ? from_26_6(FT_MulFix(os2->yStrikeoutPosition, m.y_scale)) : metrics_.ascender / 3;
  }

  hb_blob_t* blob = hb_blob_create_from_file_or_fail(file.path.string().c_str());
  if (!blob) {
    throw std::runtime_error(std::format("HarfBuzz cannot read {}", file.path.string()));
  }
  hb_face_t* hb_face = hb_face_create(blob, static_cast<unsigned>(index));
  hb_blob_destroy(blob);
  hb_font_ = hb_font_create(hb_face);
  hb_face_destroy(hb_face);
  const int hb_scale = static_cast<int>(std::lround(pixel_size * 64));
  hb_font_set_scale(hb_font_, hb_scale, hb_scale);
}

FontFace::~FontFace() {
  hb_font_destroy(hb_font_);
  FT_Done_Face(face_);
}

uint32_t FontFace::glyph_for(char32_t codepoint) const { return FT_Get_Char_Index(face_, codepoint); }

GlyphBitmap FontFace::rasterize(uint32_t glyph, int fit_height) const {
  GlyphBitmap out;
  if (FT_Load_Glyph(face_, glyph, load_flags_) != 0) {
    return out;
  }
  FT_GlyphSlot slot = face_->glyph;
  if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
    if (options_.synthetic_bold) {
      FT_GlyphSlot_Embolden(slot);
    }
    if (options_.synthetic_italic) {
      FT_GlyphSlot_Oblique(slot);
    }
  }
  if (slot->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(slot, options_.antialias ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO) != 0) {
    return out;
  }
  const FT_Bitmap& bm = slot->bitmap;
  out.width = static_cast<int>(bm.width);
  out.height = static_cast<int>(bm.rows);
  out.left = slot->bitmap_left;
  out.top = slot->bitmap_top;
  out.rgba.resize(static_cast<size_t>(out.width * out.height * 4));
  const auto& curve = coverage_curve();
  for (int y = 0; y < out.height; ++y) {
    const uint8_t* row = bm.buffer + static_cast<ptrdiff_t>(y) * bm.pitch;
    for (int x = 0; x < out.width; ++x) {
      uint8_t* p = &out.rgba[static_cast<size_t>((y * out.width + x) * 4)];
      switch (bm.pixel_mode) {
        case FT_PIXEL_MODE_GRAY:
          p[0] = p[1] = p[2] = 255;
          p[3] = curve[row[x]];
          break;
        case FT_PIXEL_MODE_MONO:
          p[0] = p[1] = p[2] = 255;
          p[3] = (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
          break;
        case FT_PIXEL_MODE_BGRA: {
          // FreeType's color bitmaps are premultiplied BGRA.
          const uint8_t* s = row + x * 4;
          const uint8_t a = s[3];
          p[0] = a ? static_cast<uint8_t>(std::min(255, s[2] * 255 / a)) : 0;
          p[1] = a ? static_cast<uint8_t>(std::min(255, s[1] * 255 / a)) : 0;
          p[2] = a ? static_cast<uint8_t>(std::min(255, s[0] * 255 / a)) : 0;
          p[3] = a;
          out.color = true;
          break;
        }
        default:
          break;
      }
    }
  }
  if (!scalable_ && fit_height > 0 && out.height > 0) {
    const double scale = fit_height / from_26_6(face_->size->metrics.y_ppem);
    const int w = std::max(1, static_cast<int>(std::lround(out.width * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(out.height * scale)));
    out.rgba = downscale(out.rgba, out.width, out.height, w, h);
    out.left = static_cast<int>(std::lround(out.left * scale));
    out.top = static_cast<int>(std::lround(out.top * scale));
    out.width = w;
    out.height = h;
  }
  return out;
}

} // namespace sailfish::render
