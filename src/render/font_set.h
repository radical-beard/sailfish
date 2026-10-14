#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "platform/font_locator.h"
#include "render/font_face.h"
#include "render/font_spec.h"

namespace sailfish::render {

// The monospace cell every glyph is laid out in, in device pixels.
struct CellMetrics {
  int width = 1;
  int height = 1;
  int baseline = 0;  // from the cell top
  int underline_y = 0;
  int underline_thickness = 1;
  int strikeout_y = 0;
};

// Resolves which face draws a cell: the 'guifont' families in order, then
// fonts bundled with Sailfish (Nerd Font symbols), then whatever the OS says
// can draw the character. The guifont and bundled faces are opened when the
// set is built (one FreeType can't open is skipped with a warning); OS
// fallbacks open on first use. Faces are shared between styles.
class FontSet {
public:
  FontSet(FontLibrary& library, platform::FontLocator& locator, const FontSpec& spec, double pixel_size, int linespace,
    std::vector<platform::FontFile> bundled);

  [[nodiscard]] const CellMetrics& cell() const { return cell_; }
  [[nodiscard]] double pixel_size() const { return pixel_size_; }
  // The family that draws text: the first installed guifont family, or the
  // first installed platform default.
  [[nodiscard]] const std::string& primary_family() const { return primary_family_; }
  FontFace& face_for(std::string_view cell_text, platform::FontStyle style);

private:
  // nullptr if FreeType can't open the file.
  FontFace* open(const platform::FontFile& file, const RasterOptions& options);
  static size_t style_index(platform::FontStyle style) { return (style.bold ? 1 : 0) | (style.italic ? 2 : 0); }

  FontLibrary& library_;
  platform::FontLocator& locator_;
  double pixel_size_;
  RasterOptions base_options_;
  CellMetrics cell_;
  std::string primary_family_;

  std::array<std::vector<FontFace*>, 4> chains_;  // by style_index; never empty
  std::vector<FontFace*> bundled_;
  std::map<std::pair<std::string, int>, std::unique_ptr<FontFace>> faces_;  // by (path + name, synthetic flags); null: unusable
  std::unordered_map<uint64_t, FontFace*> fallback_cache_;  // (codepoint, style) -> face or null
};

} // namespace sailfish::render
