#include "ui/highlight.h"

#include <utility>

namespace sailfish::ui {

void HighlightTable::define(int id, HlAttr attr) {
  if (id >= size()) {
    attrs_.resize(static_cast<size_t>(id) + 1);
  }
  attrs_[static_cast<size_t>(id)] = std::move(attr);
}

ResolvedColors HighlightTable::colors(int id) const {
  const HlAttr& a = attr(id);
  ResolvedColors c{a.fg.value_or(defaults_.fg), a.bg.value_or(defaults_.bg), a.sp.value_or(defaults_.sp)};
  if (a.reverse) {
    std::swap(c.fg, c.bg);
  }
  return c;
}

std::optional<int> HighlightTable::group(std::string_view name) const {
  if (const auto it = groups_.find(std::string(name)); it != groups_.end()) {
    return it->second;
  }
  return std::nullopt;
}

} // namespace sailfish::ui
