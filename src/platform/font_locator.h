#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sailfish::platform {

struct FontStyle {
  bool bold = false;
  bool italic = false;
  bool operator==(const FontStyle&) const = default;
};

// An installed font face. Collections (.ttc) hold several faces; the
// PostScript name says which one is meant.
struct FontFile {
  std::filesystem::path path;
  std::string postscript_name;
  bool operator==(const FontFile&) const = default;
};

// Answers "where is this font?" using the OS font database.
class FontLocator {
public:
  virtual ~FontLocator() = default;
  // The installed face of `family` closest to `style`, if the family exists.
  virtual std::optional<FontFile> find(std::string_view family, FontStyle style) = 0;
  // Some installed face that can draw `codepoint`, for text no configured font covers.
  virtual std::optional<FontFile> fallback_for(char32_t codepoint, FontStyle style) = 0;
  // Monospace families to try, in order, when 'guifont' names nothing usable.
  [[nodiscard]] virtual std::vector<std::string> default_families() const = 0;
};

// The implementation for the OS this binary was built for.
std::unique_ptr<FontLocator> make_font_locator();

} // namespace sailfish::platform
