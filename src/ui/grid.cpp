#include "ui/grid.h"

#include <algorithm>
#include <utility>

namespace sailfish::ui {

void Grid::resize(int width, int height) {
  std::vector<Cell> cells(static_cast<size_t>(width * height));
  for (int r = 0; r < std::min(height, height_); ++r) {
    for (int c = 0; c < std::min(width, width_); ++c) {
      cells[static_cast<size_t>(r * width + c)] = std::move(cell(r, c));
    }
  }
  cells_ = std::move(cells);
  scrolled_out_ = {};
  width_ = width;
  height_ = height;
  row_ids_.assign(static_cast<size_t>(height), 0);
  for (int r = 0; r < height; ++r) {
    touch(r);
  }
}

void Grid::clear() {
  scrolled_out_ = {};
  std::fill(cells_.begin(), cells_.end(), Cell{});
  for (int r = 0; r < height_; ++r) {
    touch(r);
  }
}

int Grid::put(int row, int col, std::string_view text, int hl, int repeat) {
  for (int i = 0; i < repeat && col < width_; ++i, ++col) {
    Cell& c = cell(row, col);
    c.text.assign(text);
    c.hl = hl;
  }
  touch(row);
  return col;
}

void Grid::scroll(int top, int bot, int left, int right, int rows) {
  // Keep what this scroll destroys, for smooth scrolling. Only consistent
  // full-width scrolls in one direction describe a viewport move.
  if (left == 0 && right == width_ && (scrolled_out_.scrolled_rows == 0 || (scrolled_out_.scrolled_rows > 0) == (rows > 0))) {
    const int first = rows > 0 ? top : bot + rows;
    const int last = rows > 0 ? top + rows : bot;
    std::vector<std::vector<Cell>> destroyed;
    for (int r = first; r < last; ++r) {
      destroyed.emplace_back(cells_.begin() + r * width_, cells_.begin() + (r + 1) * width_);
    }
    auto& kept = scrolled_out_.rows;
    // Scrolling up loses rows off the top: later losses sit below earlier ones.
    kept.insert(rows > 0 ? kept.end() : kept.begin(), destroyed.begin(), destroyed.end());
    scrolled_out_.scrolled_rows += rows;
  } else {
    scrolled_out_ = {};
  }
  const auto copy_row = [&](int dst, int src) {
    for (int c = left; c < right; ++c) {
      cell(dst, c) = std::move(cell(src, c));
    }
    // A full-width row keeps its identity (and anything cached for it).
    if (left == 0 && right == width_) {
      row_ids_[static_cast<size_t>(dst)] = row_ids_[static_cast<size_t>(src)];
    } else {
      touch(dst);
    }
  };
  if (rows > 0) {
    for (int r = top; r < bot - rows; ++r) {
      copy_row(r, r + rows);
    }
  } else {
    for (int r = bot - 1; r >= top - rows; --r) {
      copy_row(r, r + rows);
    }
  }
  // The vacated rows are about to be redrawn by grid_line; give them fresh ids.
  const int first_vacated = rows > 0 ? bot - rows : top;
  const int last_vacated = rows > 0 ? bot : top - rows;
  for (int r = first_vacated; r < last_vacated; ++r) {
    touch(r);
  }
}

} // namespace sailfish::ui
