#include "render/font_set.h"

#include <cmath>
#include <stdexcept>

#include "log/log.h"
#include "text/utf8.h"

namespace sailfish::render {

namespace {

constexpr char32_t kEmojiPresentation = 0xfe0f;

bool wants_color(std::string_view text) {
  for (size_t pos = 0; pos < text.size();) {
    if (text::decode_utf8(text, pos) == kEmojiPresentation) {
      return true;
    }
  }
  return false;
}

int round_px(double v) { return static_cast<int>(std::lround(v)); }

} // namespace

FontSet::FontSet(FontLibrary& library, platform::FontLocator& locator, const FontSpec& spec, double pixel_size, int linespace,
  std::vector<platform::FontFile> bundled)
  : library_(library), locator_(locator), pixel_size_(pixel_size) {
  base_options_.hinting = spec.hinting;
  base_options_.antialias = spec.antialias;

  std::vector<std::string> families;
  for (const auto& family : spec.families) {
    if (locator.find(family, {})) {
      families.push_back(family);
    } else {
      log::warn("guifont family '{}' is not installed", family);
    }
  }
  if (families.empty()) {
    for (const auto& family : locator.default_families()) {
      if (locator.find(family, {})) {
        families.push_back(family);
        break;
      }
    }
  }
  if (families.empty()) {
    throw std::runtime_error("no usable monospace font found");
  }
  primary_family_ = families.front();

  for (int bold = 0; bold < 2; ++bold) {
    for (int italic = 0; italic < 2; ++italic) {
      const platform::FontStyle style{bold == 1 || spec.bold, italic == 1 || spec.italic};
      auto& chain = chains_[style_index({bold == 1, italic == 1})];
      for (const auto& family : families) {
        const auto regular = locator.find(family, {});
        const auto styled = locator.find(family, style);
        RasterOptions options = base_options_;
        // A family without that face answers with its regular one: synthesize.
        options.synthetic_bold = style.bold && styled == regular;
        options.synthetic_italic = style.italic && styled == regular;
        if (FontFace* f = open(styled.value_or(*regular), options)) {
          chain.push_back(f);
        }
      }
      if (chain.empty()) {
        throw std::runtime_error("no usable monospace font found");
      }
    }
  }
  for (const auto& file : bundled) {
    if (FontFace* f = open(file, base_options_)) {
      bundled_.push_back(f);
    }
  }

  const FaceMetrics& m = chains_[0].front()->metrics();
  cell_.width = std::max(1, round_px(m.advance));
  const int ascent = static_cast<int>(std::ceil(m.ascender));
  const int descent = static_cast<int>(std::ceil(-m.descender));
  cell_.height = std::max(1, ascent + descent + linespace);
  cell_.baseline = ascent + linespace / 2;
  cell_.underline_thickness = std::max(1, round_px(m.underline_thickness));
  cell_.underline_y = std::min(cell_.height - cell_.underline_thickness, cell_.baseline - round_px(m.underline_position));
  cell_.strikeout_y = cell_.baseline - round_px(m.strikeout_position);
  log::info("font: {} at {:.1f}px, cell {}x{}", chains_[0].front()->file().path.filename().string(), pixel_size, cell_.width, cell_.height);
}

FontFace* FontSet::open(const platform::FontFile& file, const RasterOptions& options) {
  const int synthetic = (options.synthetic_bold ? 1 : 0) | (options.synthetic_italic ? 2 : 0);
  auto [it, inserted] = faces_.try_emplace({file.path.string() + "\n" + file.postscript_name, synthetic});
  if (inserted) {
    try {
      it->second = std::make_unique<FontFace>(library_.get(), file, pixel_size_, options);
    } catch (const std::runtime_error& e) {
      log::warn("skipping a font: {}", e.what());
    }
  }
  return it->second.get();
}

FontFace& FontSet::face_for(std::string_view cell_text, platform::FontStyle style) {
  size_t pos = 0;
  const char32_t cp = cell_text.empty() ? U' ' : text::decode_utf8(cell_text, pos);
  const bool color = wants_color(cell_text);
  const auto& chain = chains_[style_index(style)];

  const std::vector<FontFace*>* lists[] = {&chain, &bundled_};
  for (const auto* list : lists) {
    for (FontFace* f : *list) {
      if ((!color || f->has_color()) && f->glyph_for(cp) != 0) {
        return *f;
      }
    }
  }

  const uint64_t key = (static_cast<uint64_t>(cp) << 2) | style_index(style);
  auto [it, inserted] = fallback_cache_.try_emplace(key, nullptr);
  if (inserted) {
    if (const auto file = locator_.fallback_for(cp, style)) {
      FontFace* f = open(*file, base_options_);
      if (f && f->glyph_for(cp) != 0) {
        it->second = f;
      }
    }
  }
  return it->second ? *it->second : *chain.front();
}

} // namespace sailfish::render
