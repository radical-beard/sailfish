#pragma once

namespace sailfish::text {

// Terminal-style display width: 0 for combining marks, joiners and variation
// selectors (they join the previous cell), 2 for East Asian wide characters
// and emoji, 1 otherwise. Neovim lays out its own grid; this is for text the
// host lays out itself (the cmdline, messages, menus).
int char_width(char32_t cp);

} // namespace sailfish::text
