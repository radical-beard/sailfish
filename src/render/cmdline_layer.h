#pragma once
#include "render/layer.h"

namespace sailfish::render {

// Where the active cmdline's text sits in the command bar.
struct CmdlineLayout {
  ui::StyledLine line;   // firstc, prompt, indent, then the content
  int prefix_width = 0;  // cells before the content starts
  int cursor_col = 0;    // cursor cell within `line`
  int scroll = 0;        // cells scrolled off the left to keep the cursor visible
};

CmdlineLayout layout_cmdline(const ui::CmdlineLevel& level, int columns);

// The externalized cmdline (ext_cmdline), drawn in the command bar, with any
// multi-line context block (an interactive :function) stacked above it.
class CmdlineLayer final : public Layer {
public:
  void draw(Frame& frame) override;
};

} // namespace sailfish::render
