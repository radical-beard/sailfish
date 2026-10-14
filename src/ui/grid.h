#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sailfish::ui {

struct Cell {
  std::string text = " ";  // one grapheme cluster; "" marks the right half of a wide char
  int hl = 0;
};

// A screen grid as Neovim describes it. Each row carries a content id that
// changes whenever the row's cells change and travels with the row when it
// is scrolled, so a renderer can cache per-row work keyed by that id.
class Grid {
public:
  void resize(int width, int height);
  void clear();
  // Writes `text` with highlight `hl` into `repeat` consecutive cells.
  // Returns the column after the last cell written.
  int put(int row, int col, std::string_view text, int hl, int repeat);
  // grid_scroll semantics: rows > 0 moves the region [top, bot) up.
  void scroll(int top, int bot, int left, int right, int rows);

  // Rows destroyed by full-width scrolls since the last take, in screen
  // order: the content a smooth-scroll animation slides away from.
  // `scrolled_rows` is their signed count (positive: content moved up).
  struct ScrolledOut {
    std::vector<std::vector<Cell>> rows;
    int scrolled_rows = 0;
  };
  ScrolledOut take_scrolled_out() { return std::exchange(scrolled_out_, {}); }

  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] const Cell& at(int row, int col) const { return cells_[static_cast<size_t>(row * width_ + col)]; }
  [[nodiscard]] uint64_t row_id(int row) const { return row_ids_[static_cast<size_t>(row)]; }

private:
  Cell& cell(int row, int col) { return cells_[static_cast<size_t>(row * width_ + col)]; }
  void touch(int row) { row_ids_[static_cast<size_t>(row)] = ++last_row_id_; }

  int width_ = 0;
  int height_ = 0;
  std::vector<Cell> cells_;
  std::vector<uint64_t> row_ids_;
  ScrolledOut scrolled_out_;
  // Ids are unique per process so caches keyed by them never collide across grids.
  static inline uint64_t last_row_id_ = 0;
};

} // namespace sailfish::ui
