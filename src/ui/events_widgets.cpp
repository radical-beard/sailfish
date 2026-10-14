#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/redraw_events.h"

// Externalized widgets: cmdline, messages and popupmenu.
namespace sailfish::ui {

namespace {

// `[attr_id, text, hl_id]` chunks. The attribute id says how to draw a chunk;
// hl_id only names the highlight group behind it.
StyledLine parse_styled_line(const UiState& s, const msgpack::Value& chunks) {
  StyledLine line;
  for (const auto& chunk : chunks.as_array()) {
    const auto& c = chunk.as_array();
    line.push_back({hl_arg(s, c.at(0)), c.at(1).as_string()});
  }
  return line;
}

MessageKind parse_kind(std::string_view wire) {
  static constexpr std::array<std::pair<std::string_view, MessageKind>, 8> kKinds{{
    {"empty", MessageKind::Empty},
    {"confirm", MessageKind::Confirm},
    {"search_cmd", MessageKind::SearchCmd},
    {"search_count", MessageKind::SearchCount},
    {"emsg", MessageKind::Emsg},
    {"echoerr", MessageKind::Echoerr},
    {"lua_error", MessageKind::LuaError},
    {"rpc_error", MessageKind::RpcError},
  }};
  const auto it = std::find_if(kKinds.begin(), kKinds.end(), [&](const auto& k) { return k.first == wire; });
  return it == kKinds.end() ? MessageKind::Other : it->second;
}

// Message ids are integers or strings; integers are kept in decimal.
std::string parse_id(const msgpack::Value& v) { return v.is_string() ? v.as_string() : std::to_string(v.as_int()); }

std::vector<CmdlineLevel>::iterator find_level(UiState& s, int level) {
  auto& levels = s.cmdline.levels;
  return std::find_if(levels.begin(), levels.end(), [&](const CmdlineLevel& l) { return l.level == level; });
}

// The cmdline showing at a nesting level.
CmdlineLevel& level_arg(UiState& s, const msgpack::Value& v) {
  const auto it = find_level(s, int_arg(v));
  if (it == s.cmdline.levels.end()) {
    throw msgpack::TypeError(std::format("no cmdline is showing at level {}", v.to_debug_string()));
  }
  return *it;
}

void cmdline_show(UiState& s, const msgpack::Array& a) {
  CmdlineLevel line;
  line.content = parse_styled_line(s, a.at(0));
  line.pos = count_arg(a.at(1));
  line.firstc = a.at(2).as_string();
  line.prompt = a.at(3).as_string();
  line.indent = count_arg(a.at(4));
  line.level = bounded_arg(a.at(5), 1, std::numeric_limits<int>::max());
  if (const auto existing = find_level(s, line.level); existing != s.cmdline.levels.end()) {
    *existing = std::move(line);
  } else {
    s.cmdline.levels.push_back(std::move(line));
  }
}

void msg_show(UiState& s, const msgpack::Array& a) {
  Message m;
  m.kind = parse_kind(a.at(0).as_string());
  m.content = parse_styled_line(s, a.at(1));
  const bool replace_last = a.at(2).as_bool();
  m.append = a.at(4).as_bool();
  m.id = parse_id(a.at(5));
  m.trigger = a.at(6).as_string();

  m.seq = ++s.messages.generation;
  auto& visible = s.messages.visible;
  auto same_id = std::find_if(visible.begin(), visible.end(), [&](const Message& v) { return !m.id.empty() && v.id == m.id; });
  if (same_id != visible.end()) {
    *same_id = m;
  } else if (replace_last && !visible.empty()) {
    visible.back() = m;
  } else {
    visible.push_back(m);
  }
  s.messages.unread.push_back(std::move(m));
}

void msg_history_show(UiState& s, const msgpack::Array& a) {
  std::vector<Message> history;
  for (const auto& entry : a.at(0).as_array()) {
    const auto& e = entry.as_array();
    Message m;
    m.kind = parse_kind(e.at(0).as_string());
    m.content = parse_styled_line(s, e.at(1));
    m.append = e.at(2).as_bool();
    history.push_back(std::move(m));
  }
  s.messages.history = std::move(history);
  ++s.messages.history_generation;
}

void popupmenu_show(UiState& s, const msgpack::Array& a) {
  std::vector<PopupmenuItem> items;
  for (const auto& item : a.at(0).as_array()) {
    const auto& i = item.as_array();
    items.push_back({i.at(0).as_string(), i.at(1).as_string(), i.at(2).as_string()});
  }
  const int selected = bounded_arg(a.at(1), -1, static_cast<int>(items.size()) - 1);
  const int row = int_arg(a.at(2));
  const int col = int_arg(a.at(3));
  const int grid = int_arg(a.at(4));
  s.popupmenu = {.visible = true, .items = std::move(items), .selected = selected, .row = row, .col = col, .grid = grid};
}

} // namespace

void register_cmdline_events(RedrawDispatcher& d) {
  d.add("cmdline_show", cmdline_show);
  d.add("cmdline_pos", [](UiState& s, const msgpack::Array& a) {
    const int pos = count_arg(a.at(0));
    level_arg(s, a.at(1)).pos = pos;
  });
  d.add("cmdline_special_char", [](UiState& s, const msgpack::Array& a) {
    const std::string& c = a.at(0).as_string();
    level_arg(s, a.at(2)).special_char = c;
  });
  d.add("cmdline_hide", [](UiState& s, const msgpack::Array& a) {
    const int level = int_arg(a.at(0));
    std::erase_if(s.cmdline.levels, [&](const CmdlineLevel& l) { return l.level >= level; });
  });
  d.add("cmdline_block_show", [](UiState& s, const msgpack::Array& a) {
    std::vector<StyledLine> block;
    for (const auto& line : a.at(0).as_array()) {
      block.push_back(parse_styled_line(s, line));
    }
    s.cmdline.block = std::move(block);
  });
  d.add("cmdline_block_append", [](UiState& s, const msgpack::Array& a) { s.cmdline.block.push_back(parse_styled_line(s, a.at(0))); });
  d.add("cmdline_block_hide", [](UiState& s, const msgpack::Array&) { s.cmdline.block.clear(); });
}

void register_message_events(RedrawDispatcher& d) {
  d.add("msg_show", msg_show);
  d.add("msg_clear", [](UiState& s, const msgpack::Array&) {
    s.messages.visible.clear();
    ++s.messages.generation;
  });
  d.add("msg_showmode", [](UiState& s, const msgpack::Array& a) { s.messages.showmode = parse_styled_line(s, a.at(0)); });
  d.add("msg_showcmd", [](UiState& s, const msgpack::Array& a) { s.messages.showcmd = parse_styled_line(s, a.at(0)); });
  d.add("msg_ruler", [](UiState& s, const msgpack::Array& a) { s.messages.ruler = parse_styled_line(s, a.at(0)); });
  d.add("msg_history_show", msg_history_show);
}

void register_popupmenu_events(RedrawDispatcher& d) {
  d.add("popupmenu_show", popupmenu_show);
  d.add("popupmenu_select", [](UiState& s, const msgpack::Array& a) {
    s.popupmenu.selected = bounded_arg(a.at(0), -1, static_cast<int>(s.popupmenu.items.size()) - 1);
  });
  d.add("popupmenu_hide", [](UiState& s, const msgpack::Array&) { s.popupmenu.visible = false; });
}

} // namespace sailfish::ui
