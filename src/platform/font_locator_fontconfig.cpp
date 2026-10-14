#include <fontconfig/fontconfig.h>

#include <strings.h>

#include "platform/font_locator.h"

namespace sailfish::platform {

namespace {

class Pattern {
public:
  explicit Pattern(FcPattern* p) : p_(p) {}
  Pattern(const Pattern&) = delete;
  Pattern& operator=(const Pattern&) = delete;
  ~Pattern() {
    if (p_) {
      FcPatternDestroy(p_);
    }
  }
  [[nodiscard]] FcPattern* get() const { return p_; }

private:
  FcPattern* p_;
};

void add_style(FcPattern* p, FontStyle style) {
  FcPatternAddInteger(p, FC_WEIGHT, style.bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
  FcPatternAddInteger(p, FC_SLANT, style.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
}

std::optional<FontFile> file_of(FcPattern* match) {
  FcChar8* file = nullptr;
  if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch) {
    return std::nullopt;
  }
  FcChar8* postscript = nullptr;
  FcPatternGetString(match, FC_POSTSCRIPT_NAME, 0, &postscript);
  return FontFile{reinterpret_cast<const char*>(file), postscript ? reinterpret_cast<const char*>(postscript) : ""};
}

class FontconfigLocator final : public FontLocator {
public:
  FontconfigLocator() : config_(FcInitLoadConfigAndFonts()) {}
  ~FontconfigLocator() override { FcConfigDestroy(config_); }

  std::optional<FontFile> find(std::string_view family, FontStyle style) override {
    const std::string name(family);
    Pattern pattern(FcPatternCreate());
    FcPatternAddString(pattern.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>(name.c_str()));
    add_style(pattern.get(), style);
    Pattern match(resolve(pattern));
    if (!match.get()) {
      return std::nullopt;
    }
    // Fontconfig always proposes *some* font; only accept the family asked for.
    FcChar8* got = nullptr;
    for (int i = 0; FcPatternGetString(match.get(), FC_FAMILY, i, &got) == FcResultMatch; ++i) {
      if (::strcasecmp(reinterpret_cast<const char*>(got), name.c_str()) == 0) {
        return file_of(match.get());
      }
    }
    return std::nullopt;
  }

  std::optional<FontFile> fallback_for(char32_t codepoint, FontStyle style) override {
    FcCharSet* wanted = FcCharSetCreate();
    FcCharSetAddChar(wanted, codepoint);
    Pattern pattern(FcPatternCreate());
    FcPatternAddCharSet(pattern.get(), FC_CHARSET, wanted);
    FcCharSetDestroy(wanted);
    add_style(pattern.get(), style);
    Pattern match(resolve(pattern));
    FcCharSet* covered = nullptr;
    if (!match.get() || FcPatternGetCharSet(match.get(), FC_CHARSET, 0, &covered) != FcResultMatch || !FcCharSetHasChar(covered, codepoint)) {
      return std::nullopt;
    }
    return file_of(match.get());
  }

  [[nodiscard]] std::vector<std::string> default_families() const override {
    return {"JetBrainsMono Nerd Font Mono", "JetBrains Mono", "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono"};
  }

private:
  FcPattern* resolve(const Pattern& pattern) {
    FcConfigSubstitute(config_, pattern.get(), FcMatchPattern);
    FcDefaultSubstitute(pattern.get());
    FcResult result;
    return FcFontMatch(config_, pattern.get(), &result);
  }

  FcConfig* config_;
};

} // namespace

std::unique_ptr<FontLocator> make_font_locator() { return std::make_unique<FontconfigLocator>(); }

} // namespace sailfish::platform
