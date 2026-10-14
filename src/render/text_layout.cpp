#include "render/text_layout.h"

#include "text/char_width.h"
#include "text/utf8.h"

namespace sailfish::render {

namespace {

constexpr int kTabStop = 8;

// Calls emit(cluster_text, width) for each display cell group of `text`.
template <typename Emit>
void for_each_cluster(std::string_view text, int start_col, Emit emit) {
  int col = start_col;
  std::string cluster;
  int cluster_width = 0;
  const auto flush = [&] {
    if (!cluster.empty()) {
      emit(cluster, cluster_width);
      col += cluster_width;
      cluster.clear();
    }
  };
  for (size_t pos = 0; pos < text.size();) {
    const char32_t cp = text::decode_utf8(text, pos);
    if (cp == '\t') {
      flush();
      const int spaces = kTabStop - col % kTabStop;
      for (int i = 0; i < spaces; ++i) {
        emit(std::string(" "), 1);
        ++col;
      }
      continue;
    }
    if (cp < 0x20 || cp == 0x7f) {
      flush();
      cluster = {'^', static_cast<char>(cp == 0x7f ? '?' : cp + '@')};
      cluster_width = 2;
      flush();
      continue;
    }
    const int w = text::char_width(cp);
    if (w == 0 && !cluster.empty()) {
      text::append_utf8(cluster, cp);
      continue;
    }
    flush();
    text::append_utf8(cluster, cp);
    cluster_width = w == 0 ? 1 : w;
  }
  flush();
}

} // namespace

int text_width(std::string_view text) {
  int width = 0;
  for_each_cluster(text, 0, [&](const std::string&, int w) { width += w; });
  return width;
}

int put_text(ui::Grid& grid, int row, int col, std::string_view text, int hl) {
  for_each_cluster(text, col, [&](const std::string& cluster, int w) {
    if (col + w <= grid.width()) {
      grid.put(row, col, cluster, hl, 1);
      if (w == 2) {
        grid.put(row, col + 1, "", hl, 1);
      }
    }
    col += w;
  });
  return col;
}

int put_line(ui::Grid& grid, int row, int col, const ui::StyledLine& line) {
  for (const auto& chunk : line) {
    col = put_text(grid, row, col, chunk.text, chunk.hl);
  }
  return col;
}

std::vector<ui::StyledLine> split_lines(const ui::StyledLine& text) {
  std::vector<ui::StyledLine> lines(1);
  for (const auto& chunk : text) {
    std::string_view rest = chunk.text;
    for (size_t nl = rest.find('\n'); nl != std::string_view::npos; nl = rest.find('\n')) {
      if (nl > 0) {
        lines.back().push_back({chunk.hl, std::string(rest.substr(0, nl))});
      }
      lines.emplace_back();
      rest.remove_prefix(nl + 1);
    }
    if (!rest.empty()) {
      lines.back().push_back({chunk.hl, std::string(rest)});
    }
  }
  return lines;
}

std::vector<ui::StyledLine> wrap_words(const std::vector<ui::StyledLine>& lines, int width) {
  struct Cell {
    std::string text;
    int hl;
    int width;
  };
  std::vector<ui::StyledLine> out;
  for (const auto& line : lines) {
    std::vector<Cell> cells;
    int col = 0;
    for (const auto& chunk : line) {
      for_each_cluster(chunk.text, col, [&](const std::string& cluster, int w) {
        cells.push_back({cluster, chunk.hl, w});
        col += w;
      });
    }
    size_t start = 0;
    do {
      // The longest run from `start` that fits (at least one cell), cut back
      // to just after its last space when the line continues past it.
      size_t end = start;
      size_t after_space = start;
      for (int used = 0; end < cells.size() && (end == start || used + cells[end].width <= width); ++end) {
        used += cells[end].width;
        if (cells[end].text == " ") {
          after_space = end + 1;
        }
      }
      if (end < cells.size() && after_space > start) {
        end = after_space;
      }
      out.emplace_back();
      for (size_t i = start; i < end; ++i) {
        if (out.back().empty() || out.back().back().hl != cells[i].hl) {
          out.back().push_back({cells[i].hl, ""});
        }
        out.back().back().text += cells[i].text;
      }
      start = end;
    } while (start < cells.size());
  }
  return out;
}

std::vector<ui::StyledLine> wrap_lines(const std::vector<ui::StyledLine>& lines, int width) {
  std::vector<ui::StyledLine> out;
  for (const auto& line : lines) {
    out.emplace_back();
    int col = 0;
    for (const auto& chunk : line) {
      for_each_cluster(chunk.text, col, [&](const std::string& cluster, int w) {
        if (col + w > width && col > 0) {
          out.emplace_back();
          col = 0;
        }
        if (out.back().empty() || out.back().back().hl != chunk.hl) {
          out.back().push_back({chunk.hl, ""});
        }
        out.back().back().text += cluster;
        col += w;
      });
    }
  }
  return out;
}

} // namespace sailfish::render
