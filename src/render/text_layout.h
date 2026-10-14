#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "ui/grid.h"
#include "ui/ui_state.h"

namespace sailfish::render {

// Display width of text in cells.
int text_width(std::string_view text);

// Lays UTF-8 text into cells like Neovim does: wide characters take two
// cells (the second holds ""), zero-width characters join the previous cell,
// tabs expand and control characters show as ^X. Returns the column after
// the text; whatever falls past the grid's edge is dropped.
int put_text(ui::Grid& grid, int row, int col, std::string_view text, int hl);
int put_line(ui::Grid& grid, int row, int col, const ui::StyledLine& line);

// Splits styled text on '\n' into display lines.
std::vector<ui::StyledLine> split_lines(const ui::StyledLine& text);

// Soft-wraps display lines to at most `width` cells each, breaking anywhere
// (the way Neovim wraps message output).
std::vector<ui::StyledLine> wrap_lines(const std::vector<ui::StyledLine>& lines, int width);

// Soft-wraps prose to at most `width` cells per line, breaking after a space
// where there is one (a word longer than a line still breaks inside).
std::vector<ui::StyledLine> wrap_words(const std::vector<ui::StyledLine>& lines, int width);

} // namespace sailfish::render
