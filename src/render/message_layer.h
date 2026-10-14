#pragma once
#include <cstdint>

#include "render/layer.h"

namespace sailfish::render {

// Whether a message is shown as a transient toast instead (render/toast_layer):
// nothing the user typed produced it, and it isn't part of a prompt.
bool is_toast(const ui::Message& m);

// Externalized messages (ext_messages), Neovim-style: the latest message line
// sits in the command bar, earlier lines of a multi-line output (:ls, errors
// with tracebacks, :messages) stack in a panel above it. There is no
// "Press ENTER" prompt: the next key press dismisses the panel and still
// reaches Neovim. 'showmode', 'showcmd' and the ruler share the bar.
class MessageLayer final : public Layer {
public:
  void draw(Frame& frame) override;
  // A key was pressed: put away the panel (the bar keeps the last line).
  void on_key() override;
  // Message numbers start over in every editor.
  void reset() override { *this = MessageLayer(); }

private:
  uint64_t hidden_through_ = 0;     // messages up to this seq were cleared by the cmdline opening
  uint64_t dismissed_through_ = 0;  // panel lines up to this seq were dismissed
  uint64_t history_dismissed_ = 0;  // history listings up to this generation were dismissed
  uint64_t latest_seq_ = 0;
  uint64_t latest_history_ = 0;
};

} // namespace sailfish::render
