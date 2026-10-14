#pragma once
#include <string>
#include <string_view>

// Neovim key notation (`:h key-notation`) for keys a GUI can see. Unlike a
// terminal, a GUI can tell <Tab> from <C-i>, <CR> from <C-m>, and can send
// <S-CR>, <C-S-a> or <D-s> (Cmd/Super).
namespace sailfish::input {

struct Modifiers {
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool super = false;  // Cmd on macOS; Neovim's <D-...>

  [[nodiscard]] bool any() const { return shift || ctrl || alt || super; }
};

enum class NamedKey {
  Escape, Enter, Tab, Backspace, Delete, Insert, Space,
  Home, End, PageUp, PageDown, Up, Down, Left, Right,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
};

// A key the keyboard names rather than types: <Esc>, <S-Tab>, <C-F5>.
std::string encode_named(NamedKey key, Modifiers mods);

// A character key pressed with Ctrl, Alt-as-Meta or Super, which produce no
// text of their own. `base` is the key's unshifted character on the active
// layout and `shifted` what Shift makes of it ('2' and '@' on US layouts).
std::string encode_chord(char32_t base, char32_t shifted, Modifiers mods);

// Typed text, escaped for nvim_input().
std::string encode_text(std::string_view utf8);

} // namespace sailfish::input
