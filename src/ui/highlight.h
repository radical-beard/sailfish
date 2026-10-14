#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sailfish::ui {

using Rgb = uint32_t;  // 0xRRGGBB

enum class Underline : uint8_t { None, Line, Curl, Double, Dotted, Dashed };

// One `hl_attr_define` entry. Absent colors mean "use the default color", so a
// later `default_colors_set` re-themes every cell that relies on it.
struct HlAttr {
  std::optional<Rgb> fg;
  std::optional<Rgb> bg;
  std::optional<Rgb> sp;
  bool reverse = false;
  bool italic = false;
  bool bold = false;
  bool strikethrough = false;
  bool overline = false;
  Underline underline = Underline::None;
  int blend = 0;  // 0..100
};

struct DefaultColors {
  Rgb fg = 0xffffff;
  Rgb bg = 0x000000;
  Rgb sp = 0xff0000;
};

// Colors to draw with once defaults and reverse video are resolved.
struct ResolvedColors {
  Rgb fg;
  Rgb bg;
  Rgb sp;
};

class HighlightTable {
public:
  void define(int id, HlAttr attr);
  void set_defaults(DefaultColors colors) { defaults_ = colors; }
  void set_group(std::string name, int id) { groups_[std::move(name)] = id; }

  // Ids below size() are defined: 0 is the default attribute, every other id
  // came from hl_attr_define. The redraw boundary admits no other ids.
  [[nodiscard]] int size() const { return static_cast<int>(attrs_.size()); }
  [[nodiscard]] const HlAttr& attr(int id) const { return attrs_.at(static_cast<size_t>(id)); }
  [[nodiscard]] ResolvedColors colors(int id) const;
  [[nodiscard]] const DefaultColors& defaults() const { return defaults_; }
  // Attribute id of a builtin highlight group, from `hl_group_set`.
  [[nodiscard]] std::optional<int> group(std::string_view name) const;

private:
  std::vector<HlAttr> attrs_ = std::vector<HlAttr>(1);  // Neovim ids are small and dense
  DefaultColors defaults_;
  std::unordered_map<std::string, int> groups_;
};

} // namespace sailfish::ui
