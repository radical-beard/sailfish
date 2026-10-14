#include "render/message_layer.h"

#include <algorithm>
#include <vector>

#include "render/text_layout.h"

namespace sailfish::render {

namespace {

// Display lines of a run of messages; `append` messages (:echon) continue
// the previous line instead of starting a new one.
std::vector<ui::StyledLine> message_lines(const std::vector<const ui::Message*>& messages) {
  std::vector<ui::StyledLine> lines;
  for (const ui::Message* m : messages) {
    if (m->kind == ui::MessageKind::Empty) {
      continue;
    }
    auto split = split_lines(m->content);
    size_t first = 0;
    if (m->append && !lines.empty()) {
      lines.back().insert(lines.back().end(), split[0].begin(), split[0].end());
      first = 1;
    }
    lines.insert(lines.end(), split.begin() + static_cast<std::ptrdiff_t>(first), split.end());
  }
  return lines;
}

int line_width(const ui::StyledLine& line) {
  int w = 0;
  for (const auto& chunk : line) {
    w += text_width(chunk.text);
  }
  return w;
}

} // namespace

bool is_toast(const ui::Message& m) {
  using K = ui::MessageKind;
  return m.trigger.empty() && m.kind != K::Confirm && m.kind != K::Empty && m.kind != K::SearchCount && m.kind != K::SearchCmd;
}

void MessageLayer::on_key() {
  dismissed_through_ = latest_seq_;
  history_dismissed_ = latest_history_;
}

void MessageLayer::draw(Frame& f) {
  const ui::UiState& s = f.state;
  const ui::MessageState& m = s.messages;
  latest_seq_ = m.generation;
  latest_history_ = m.history_generation;
  if (!f.native_messages) {
    return;
  }
  // A prompt (confirm(), input(), the swap-file question) is a cmdline
  // without a command character; the messages explaining it stay visible.
  const bool prompting = s.cmdline.visible() && s.cmdline.levels.back().firstc.empty();
  if (s.cmdline.visible() && !prompting) {
    hidden_through_ = m.generation;  // typing a command clears the message area, as in Neovim
    return;
  }

  std::vector<const ui::Message*> shown;
  std::vector<const ui::Message*> paneled;
  if (m.history_generation > history_dismissed_) {
    for (const auto& h : m.history) {
      shown.push_back(&h);
      paneled.push_back(&h);
    }
  } else {
    for (const auto& v : m.visible) {
      // During a prompt every message explains it; otherwise toasts go elsewhere.
      if (v.seq > hidden_through_ && (prompting || !is_toast(v))) {
        shown.push_back(&v);
        if (prompting || v.seq > dismissed_through_) {
          paneled.push_back(&v);
        }
      }
    }
  }
  const CellMetrics& cell = f.cell();
  const int cols = f.columns();
  const std::vector<ui::StyledLine> lines = wrap_lines(message_lines(shown), cols);
  const std::vector<ui::StyledLine> panel_lines = wrap_lines(message_lines(paneled), cols);
  const float bar_y = f.bar_top();
  const int background = f.group("MsgArea");

  // Every line but the last stacks above the bar, at most half the window;
  // during a prompt the cmdline owns the bar, so all of them do.
  const int max_rows = std::max(1, static_cast<int>(bar_y) / cell.height / 2);
  const int stacked = static_cast<int>(panel_lines.size()) - (prompting ? 0 : 1);
  const int panel_rows = std::min(stacked, max_rows);
  if (panel_rows > 0) {
    ui::Grid panel = Frame::blank_grid(cols, panel_rows + 1, background);
    panel.put(0, 0, " ", f.group("MsgSeparator"), cols);
    const size_t first = static_cast<size_t>(stacked - panel_rows);
    for (int r = 0; r < panel_rows; ++r) {
      put_line(panel, r + 1, 0, panel_lines[first + static_cast<size_t>(r)]);
    }
    f.painter.paint(panel, s.highlights, {0, bar_y - static_cast<float>((panel_rows + 1) * cell.height)}, panel_rows + 1, cols);
  }

  if (prompting) {
    return;
  }
  ui::Grid bar = Frame::blank_grid(cols, 1, background);
  if (!lines.empty()) {
    put_line(bar, 0, 0, lines.back());
  } else {
    put_line(bar, 0, 0, m.showmode);
  }
  // showcmd and the ruler sit at the right edge, as in Neovim's cmdline row.
  const int ruler_col = cols - std::max(18, line_width(m.ruler) + 1);
  const int showcmd_col = ruler_col - 11;
  if (showcmd_col > line_width(lines.empty() ? m.showmode : lines.back())) {
    put_line(bar, 0, showcmd_col, m.showcmd);
    put_line(bar, 0, ruler_col, m.ruler);
  }
  f.painter.paint(bar, s.highlights, {0, bar_y}, 1, cols);
}

} // namespace sailfish::render
