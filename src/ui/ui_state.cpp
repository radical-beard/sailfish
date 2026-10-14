#include "ui/ui_state.h"

namespace sailfish::ui {

bool Message::is_error() const {
  return kind == MessageKind::Emsg || kind == MessageKind::Echoerr || kind == MessageKind::LuaError || kind == MessageKind::RpcError;
}

const ModeInfo* UiState::current_mode() const {
  if (mode_index < 0 || mode_index >= static_cast<int>(modes.size())) {
    return nullptr;
  }
  return &modes[static_cast<size_t>(mode_index)];
}

} // namespace sailfish::ui
