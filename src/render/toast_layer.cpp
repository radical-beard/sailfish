#include "render/toast_layer.h"

#include <algorithm>

#include "render/message_layer.h"
#include "render/text_layout.h"

namespace sailfish::render {

namespace {

constexpr double kSeconds = 4.0;
constexpr double kErrorSeconds = 8.0;
constexpr double kFadeSeconds = 0.4;
constexpr size_t kMaxToasts = 5;
constexpr int kMaxLines = 6;

} // namespace

void ToastLayer::collect(const Frame& f) {
  for (const auto& m : f.state.messages.visible) {
    if (m.seq <= seen_through_ || !is_toast(m)) {
      continue;
    }
    const double lifetime = m.is_error() ? kErrorSeconds : kSeconds;
    auto same = std::find_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return !m.id.empty() && t.message.id == m.id; });
    if (same != toasts_.end()) {
      *same = {m, f.now, lifetime};
    } else {
      toasts_.push_back({m, f.now, lifetime});
    }
  }
  seen_through_ = f.state.messages.generation;
  std::erase_if(toasts_, [&](const Toast& t) { return f.now - t.shown_at > t.lifetime; });
  while (toasts_.size() > kMaxToasts) {
    toasts_.pop_front();
  }
}

void ToastLayer::draw(Frame& f) {
  collect(f);
  if (!f.native_messages) {
    return;
  }
  const CellMetrics& cell = f.cell();
  const int cols = f.columns();
  const int width = std::max(24, cols / 3);
  const int background = f.group("NormalFloat");
  float y = static_cast<float>(cell.height) * 0.5f;

  for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
    const Toast& t = *it;
    auto lines = wrap_words(split_lines(t.message.content), width - 2);
    std::erase_if(lines, [](const ui::StyledLine& l) { return l.empty(); });
    if (lines.empty()) {
      continue;
    }
    if (static_cast<int>(lines.size()) > kMaxLines) {
      lines.resize(kMaxLines);
      lines.back() = {{0, "…"}};
    }
    const int rows = static_cast<int>(lines.size());
    ui::Grid box = Frame::blank_grid(width, rows, background);
    for (int r = 0; r < rows; ++r) {
      put_line(box, r, 1, lines[static_cast<size_t>(r)]);
    }
    const float x = static_cast<float>((cols - width - 1) * cell.width);
    const float h = static_cast<float>(rows * cell.height);
    // Fade out at the end of the toast's life.
    const double remaining = t.lifetime - (f.now - t.shown_at);
    const float alpha = static_cast<float>(std::clamp(remaining / kFadeSeconds, 0.0, 1.0));
    const ui::Rgb accent = t.message.is_error() ? f.state.highlights.colors(f.group("ErrorMsg")).fg : f.state.highlights.colors(f.group("FloatBorder")).fg;
    f.batch.fill({x - 2, y - 2, static_cast<float>(width * cell.width) + 4, h + 4}, {0, 0, 0, 0.25f * alpha});
    f.painter.paint(box, f.state.highlights, {x, y}, rows, width);
    if (alpha < 1.0f) {
      // A veil in the editor's background color reads as fading without
      // re-tinting every glyph.
      f.batch.fill({x, y, static_cast<float>(width * cell.width), h}, to_color(f.state.highlights.defaults().bg, 1.0f - alpha));
    }
    f.batch.fill({x, y, std::max(2.0f, static_cast<float>(cell.width) / 4.0f), h}, to_color(accent, alpha));
    y += h + static_cast<float>(cell.height) * 0.5f;
  }
}

std::optional<double> ToastLayer::next_frame_in(const Frame& f) const {
  std::optional<double> next;
  for (const Toast& t : toasts_) {
    const double fade_start = t.shown_at + t.lifetime - kFadeSeconds;
    // Animate through the fade; otherwise wake when it begins.
    const double wait = f.now >= fade_start ? 1.0 / 60.0 : fade_start - f.now;
    next = next ? std::min(*next, wait) : wait;
  }
  return next;
}

} // namespace sailfish::render
