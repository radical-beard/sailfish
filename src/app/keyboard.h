#pragma once
#include <SDL3/SDL.h>

#include <functional>
#include <string>

namespace sailfish::app {

// Turns SDL keyboard events into Neovim input. A key press arrives as
// KEY_DOWN and, for printable keys, a following TEXT_INPUT; exactly one of
// the two produces input:
//   named keys (Esc, F5, arrows)          -> KEY_DOWN, e.g. <S-Tab>
//   chords with Ctrl / Cmd / Alt-as-Meta  -> KEY_DOWN, e.g. <C-S-a>, <D-s>
//   everything else                       -> TEXT_INPUT, so layouts, dead
//                                            keys, AltGr and IMEs just work
class Keyboard {
public:
  Keyboard(std::function<void(std::string)> send, bool alt_is_meta) : send_(std::move(send)), alt_is_meta_(alt_is_meta) {}

  void key_down(const SDL_KeyboardEvent& e);
  // The press is over; text arriving after this has no key of its own
  // (dictation, the character viewer) and is always input.
  void key_up() { suppress_text_ = false; }
  void text_input(const SDL_TextInputEvent& e);
  void text_editing(const SDL_TextEditingEvent& e);

private:
  std::function<void(std::string)> send_;
  bool alt_is_meta_;
  bool suppress_text_ = false;  // the key being pressed already produced input
  bool composing_ = false;      // an IME composition owns the keyboard
};

} // namespace sailfish::app
