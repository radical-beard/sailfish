#include "app/keyboard.h"

#include <optional>

#include "input/key_encoding.h"

namespace sailfish::app {

namespace {

std::optional<input::NamedKey> named_key(SDL_Keycode key) {
  using K = input::NamedKey;
  switch (key) {
    case SDLK_ESCAPE: return K::Escape;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return K::Enter;
    case SDLK_TAB: return K::Tab;
    case SDLK_BACKSPACE: return K::Backspace;
    case SDLK_DELETE: return K::Delete;
    case SDLK_INSERT: return K::Insert;
    case SDLK_HOME: return K::Home;
    case SDLK_END: return K::End;
    case SDLK_PAGEUP: return K::PageUp;
    case SDLK_PAGEDOWN: return K::PageDown;
    case SDLK_UP: return K::Up;
    case SDLK_DOWN: return K::Down;
    case SDLK_LEFT: return K::Left;
    case SDLK_RIGHT: return K::Right;
    default: break;
  }
  // SDL numbers F1-F12 and F13-F24 as two separate runs.
  if (key >= SDLK_F1 && key <= SDLK_F12) {
    return static_cast<K>(static_cast<int>(K::F1) + static_cast<int>(key - SDLK_F1));
  }
  if (key >= SDLK_F13 && key <= SDLK_F24) {
    return static_cast<K>(static_cast<int>(K::F13) + static_cast<int>(key - SDLK_F13));
  }
  return std::nullopt;
}

input::Modifiers modifiers(SDL_Keymod mod) {
  return {(mod & SDL_KMOD_SHIFT) != 0, (mod & SDL_KMOD_CTRL) != 0, (mod & SDL_KMOD_ALT) != 0, (mod & SDL_KMOD_GUI) != 0};
}

// AltGr reaches us as Ctrl+Alt on Windows (and as MODE elsewhere); its text
// arrives as TEXT_INPUT and must not be read as a chord.
bool is_altgr(SDL_Keymod mod) {
  return (mod & SDL_KMOD_MODE) != 0 || ((mod & SDL_KMOD_LCTRL) != 0 && (mod & SDL_KMOD_RALT) != 0);
}

bool is_character_key(SDL_Keycode key) { return (key & SDLK_SCANCODE_MASK) == 0 && key >= 0x20 && key != SDLK_DELETE; }

} // namespace

void Keyboard::key_down(const SDL_KeyboardEvent& e) {
  suppress_text_ = false;
  if (composing_) {
    return;
  }
  const input::Modifiers mods = modifiers(e.mod);
  if (const auto named = named_key(e.key)) {
    send_(input::encode_named(*named, mods));
    suppress_text_ = true;
    return;
  }
  if (e.key == SDLK_SPACE && mods.any()) {
    send_(input::encode_named(input::NamedKey::Space, mods));
    suppress_text_ = true;
    return;
  }
  const bool chord = mods.ctrl || mods.super || (mods.alt && alt_is_meta_);
  if (chord && !is_altgr(e.mod) && is_character_key(e.key)) {
    const auto shifted = SDL_GetKeyFromScancode(e.scancode, SDL_KMOD_SHIFT, false);
    send_(input::encode_chord(static_cast<char32_t>(e.key), static_cast<char32_t>(shifted), mods));
    suppress_text_ = true;
  }
}

void Keyboard::text_input(const SDL_TextInputEvent& e) {
  composing_ = false;
  if (suppress_text_) {
    suppress_text_ = false;
    return;
  }
  send_(input::encode_text(e.text));
}

void Keyboard::text_editing(const SDL_TextEditingEvent& e) { composing_ = e.text && *e.text; }

} // namespace sailfish::app
