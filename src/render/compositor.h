#pragma once
#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platform/font_locator.h"
#include "render/font_set.h"
#include "render/glyph_cache.h"
#include "render/layer.h"
#include "render/quad_batch.h"
#include "render/shape_cache.h"

namespace sailfish::render {

struct FrameOptions {
  bool focused = true;
  bool native_messages = true;
  double now = 0;
};

struct FontRequest {
  FontSpec spec;
  double pixel_size = 0;
  int linespace = 0;
  bool operator==(const FontRequest&) const = default;
};

// Owns the frame: fonts, glyph atlas, shaping cache, and the ordered layers.
class Compositor {
public:
  Compositor(SDL_Renderer* renderer, platform::FontLocator& locator, std::vector<platform::FontFile> bundled_fonts, const FontRequest& font);

  // Rebuilds fonts (and drops cached glyphs) when the request changed.
  void set_font(const FontRequest& request);
  [[nodiscard]] const CellMetrics& cell() const { return fonts_->cell(); }
  [[nodiscard]] const std::string& font_family() const { return fonts_->primary_family(); }

  void add_layer(std::unique_ptr<Layer> layer) { layers_.push_back(std::move(layer)); }
  // Forwarded to every layer; see Layer.
  void key_pressed();
  void reset();

  // Draws and presents one frame. Returns seconds until another frame is
  // needed for animation, or nullopt when the picture is static.
  std::optional<double> render(const ui::UiState& state, const FrameOptions& options);

  // The next frame is also written to `path` as a BMP (debugging, tests).
  void request_screenshot(std::string path) { screenshot_path_ = std::move(path); }

private:
  SDL_Renderer* renderer_;
  platform::FontLocator& locator_;
  std::vector<platform::FontFile> bundled_fonts_;
  FontLibrary library_;
  std::unique_ptr<FontSet> fonts_;
  std::optional<FontRequest> font_request_;
  GlyphCache glyphs_;
  ShapeCache shapes_;
  QuadBatch batch_;
  std::vector<std::unique_ptr<Layer>> layers_;
  std::string screenshot_path_;
};

} // namespace sailfish::render
