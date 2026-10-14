#pragma once
#include "render/layer.h"

namespace sailfish::render {

// The externalized completion menu (ext_popupmenu): insert-mode completion
// anchored at the completed word, and the cmdline wildmenu anchored to the
// command bar. Drawn with the Pmenu* highlight groups, a scrollbar when the
// list is long, and a drop shadow.
class PopupmenuLayer final : public Layer {
public:
  void draw(Frame& frame) override;
};

} // namespace sailfish::render
