#include "render/font_spec.h"

#include <algorithm>
#include <charconv>

namespace sailfish::render {

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}

// Splits on `sep`, honoring backslash escapes ("Iosevka\ Term", "a\,b").
std::vector<std::string> split_escaped(std::string_view s, char sep) {
  std::vector<std::string> parts(1);
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      parts.back() += s[++i];
    } else if (s[i] == sep) {
      parts.emplace_back();
    } else {
      parts.back() += s[i];
    }
  }
  return parts;
}

void apply_option(FontSpec& spec, std::string_view opt) {
  if (opt.empty()) {
    return;
  }
  if (opt[0] == 'h' && opt.size() > 1) {
    double size = 0;
    const auto [end, ec] = std::from_chars(opt.data() + 1, opt.data() + opt.size(), size);
    if (ec == std::errc() && size > 0) {
      spec.size = size;
    }
  } else if (opt == "b") {
    spec.bold = true;
  } else if (opt == "i") {
    spec.italic = true;
  } else if (opt == "#h-none") {
    spec.hinting = Hinting::None;
  } else if (opt == "#h-slight" || opt == "#h-light") {
    spec.hinting = Hinting::Light;
  } else if (opt == "#h-full") {
    spec.hinting = Hinting::Full;
  } else if (opt == "#e-alias") {
    spec.antialias = false;
  } else if (opt == "#e-antialias" || opt == "#e-subpixelantialias") {
    spec.antialias = true;
  }
}

} // namespace

FontSpec parse_guifont(std::string_view guifont) {
  FontSpec spec;
  const auto sections = split_escaped(guifont, ':');
  for (const auto& family : split_escaped(sections[0], ',')) {
    std::string name(trim(family));
    std::replace(name.begin(), name.end(), '_', ' ');  // gvim's spelling of spaces
    if (!name.empty() && name != "*") {
      spec.families.push_back(std::move(name));
    }
  }
  for (size_t i = 1; i < sections.size(); ++i) {
    apply_option(spec, trim(sections[i]));
  }
  return spec;
}

} // namespace sailfish::render
