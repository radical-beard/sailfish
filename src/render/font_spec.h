#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace sailfish::render {

enum class Hinting { None, Light, Full };

inline constexpr double kDefaultPointSize = 13.0;

// A parsed 'guifont': "Fira Code,Symbols Nerd Font:h13:b:#h-none".
struct FontSpec {
  std::vector<std::string> families;  // fallback order; empty: platform defaults
  double size = kDefaultPointSize;    // points
  bool bold = false;
  bool italic = false;
  Hinting hinting = Hinting::Light;
  bool antialias = true;

  bool operator==(const FontSpec&) const = default;
};

// Unknown options are ignored so a config written for another GUI still works.
FontSpec parse_guifont(std::string_view guifont);

} // namespace sailfish::render
