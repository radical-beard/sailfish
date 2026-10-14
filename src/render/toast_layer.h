#pragma once
#include <cstdint>
#include <deque>

#include "render/layer.h"

namespace sailfish::render {

// Messages nothing the user typed produced (plugin notifications, install
// progress, errors from background work) appear as toasts stacked in the
// top-right corner and fade on their own, leaving the command bar to what
// the user asked for. A message updated by id updates its toast in place.
class ToastLayer final : public Layer {
public:
  void draw(Frame& frame) override;
  [[nodiscard]] std::optional<double> next_frame_in(const Frame& frame) const override;

private:
  struct Toast {
    ui::Message message;
    double shown_at = 0;
    double lifetime = 0;
  };

  void collect(const Frame& f);

  std::deque<Toast> toasts_;
  uint64_t seen_through_ = 0;
};

} // namespace sailfish::render
