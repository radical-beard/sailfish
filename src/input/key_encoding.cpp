#include "input/key_encoding.h"

#include <stdexcept>

#include "text/utf8.h"

namespace sailfish::input {

namespace {

std::string_view named_key_name(NamedKey key) {
  using K = NamedKey;
  switch (key) {
    case K::Escape: return "Esc";
    case K::Enter: return "CR";
    case K::Tab: return "Tab";
    case K::Backspace: return "BS";
    case K::Delete: return "Del";
    case K::Insert: return "Insert";
    case K::Space: return "Space";
    case K::Home: return "Home";
    case K::End: return "End";
    case K::PageUp: return "PageUp";
    case K::PageDown: return "PageDown";
    case K::Up: return "Up";
    case K::Down: return "Down";
    case K::Left: return "Left";
    case K::Right: return "Right";
    case K::F1: return "F1";
    case K::F2: return "F2";
    case K::F3: return "F3";
    case K::F4: return "F4";
    case K::F5: return "F5";
    case K::F6: return "F6";
    case K::F7: return "F7";
    case K::F8: return "F8";
    case K::F9: return "F9";
    case K::F10: return "F10";
    case K::F11: return "F11";
    case K::F12: return "F12";
    case K::F13: return "F13";
    case K::F14: return "F14";
    case K::F15: return "F15";
    case K::F16: return "F16";
    case K::F17: return "F17";
    case K::F18: return "F18";
    case K::F19: return "F19";
    case K::F20: return "F20";
    case K::F21: return "F21";
    case K::F22: return "F22";
    case K::F23: return "F23";
    case K::F24: return "F24";
  }
  throw std::logic_error("unreachable NamedKey");
}

// Characters that can't appear raw inside <...>.
std::string key_name(char32_t cp) {
  switch (cp) {
    case '<': return "lt";
    case '\\': return "Bslash";
    case '|': return "Bar";
    case ' ': return "Space";
    default: {
      std::string s;
      text::append_utf8(s, cp);
      return s;
    }
  }
}

std::string wrap(const Modifiers& mods, std::string_view name) {
  std::string out = "<";
  if (mods.ctrl) out += "C-";
  if (mods.alt) out += "M-";
  if (mods.super) out += "D-";
  if (mods.shift) out += "S-";
  out += name;
  out += '>';
  return out;
}

bool is_ascii_lower(char32_t c) { return c >= 'a' && c <= 'z'; }

} // namespace

std::string encode_named(NamedKey key, Modifiers mods) {
  if (!mods.any() && key == NamedKey::Space) {
    return " ";
  }
  return wrap(mods, named_key_name(key));
}

std::string encode_chord(char32_t base, char32_t shifted, Modifiers mods) {
  char32_t key = base;
  if (mods.shift && is_ascii_lower(base)) {
    // Neovim keeps Shift alongside Ctrl (<C-S-a> differs from <C-a>) but
    // otherwise expects the uppercase letter: <M-A>, <D-A>.
    if (!mods.ctrl) {
      key = base - 'a' + 'A';
      mods.shift = false;
    }
  } else if (mods.shift && shifted != base) {
    // Shift was consumed producing a symbol: Ctrl+Shift+2 is <C-@>.
    key = shifted;
    mods.shift = false;
  }
  return wrap(mods, key_name(key));
}

std::string encode_text(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  for (char c : utf8) {
    if (c == '<') {
      out += "<lt>";
    } else {
      out += c;
    }
  }
  return out;
}

} // namespace sailfish::input
