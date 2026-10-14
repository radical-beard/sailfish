#pragma once
#include <string>

#include "host/host_api.h"

namespace sailfish::host {

// Backs Neovim's "+ and "* registers with the OS clipboard (via SDL), so the
// clipboard works the same on every platform with no external tools.
class ClipboardService {
public:
  void register_methods(HostApi& api);

private:
  // Neovim distinguishes charwise from linewise yanks; the OS clipboard only
  // holds text. Remember the register type of what we last copied.
  std::string last_text_;
  std::string last_regtype_;
};

} // namespace sailfish::host
