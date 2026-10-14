#include <doctest/doctest.h>

#include <stdexcept>

#include "ui/redraw_dispatcher.h"

using namespace sailfish;
using msgpack::Array;
using msgpack::Map;
using msgpack::Value;

namespace {

// Always builds an array, even from a single array element (plain
// `Array{x}` with an Array `x` copies x instead of nesting it).
Value A(std::initializer_list<Value> items) { return Value(Array(items)); }

// One complete redraw: the events, then the flush that makes them visible.
void redraw(ui::RedrawDispatcher& d, ui::UiState& s, std::initializer_list<Value> events) {
  Array batch(events);
  batch.push_back(A({"flush", A({})}));
  d.apply(s, batch);
}

// hl_attr_define for ids 1..n, so cells and chunks may use them.
Value define_hl(int n) {
  Array event{"hl_attr_define"};
  for (int id = 1; id <= n; ++id) {
    event.push_back(A({id, Map{}, Map{}, A({})}));
  }
  return Value(std::move(event));
}

std::string row_text(const ui::Grid& g, int row) {
  std::string s;
  for (int c = 0; c < g.width(); ++c) {
    s += g.at(row, c).text;
  }
  return s;
}

} // namespace

TEST_CASE("grid_line writes cells, carries hl ids forward and expands repeats") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {
    define_hl(7),
    A({"grid_resize", A({1, 10, 2})}),
    A({"grid_line", A({1, 0, 0, A({A({"a", 5}), A({"b"}), A({"-", 6, 3}), A({"界", 7}), A({""})}), false})}),
  });
  const ui::Grid& g = s.grids.at(1);
  CHECK(row_text(g, 0) == "ab---界   ");  // the wide char's right half is ""
  CHECK(g.at(0, 1).hl == 5);
  CHECK(g.at(0, 4).hl == 6);
  CHECK(g.at(0, 6).text.empty());
  CHECK(s.changes.flushed);
}

TEST_CASE("grid_scroll moves rows and keeps their ids") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {
    A({"grid_resize", A({1, 3, 4})}),
    A({"grid_line", A({1, 0, 0, A({A({"a", 0, 3})}), false}), A({1, 1, 0, A({A({"b", 0, 3})}), false}),
      A({1, 2, 0, A({A({"c", 0, 3})}), false}), A({1, 3, 0, A({A({"d", 0, 3})}), false})}),
  });
  const uint64_t id_of_c = s.grids.at(1).row_id(2);

  redraw(d, s, {A({"grid_scroll", A({1, 0, 4, 0, 3, 2, 0})})});
  const ui::Grid& g = s.grids.at(1);
  CHECK(row_text(g, 0) == "ccc");
  CHECK(row_text(g, 1) == "ddd");
  CHECK(g.row_id(0) == id_of_c);
  CHECK(g.row_id(2) != g.row_id(0));

  redraw(d, s, {A({"grid_scroll", A({1, 0, 4, 0, 3, -1, 0})})});
  CHECK(row_text(s.grids.at(1), 1) == "ccc");
}

TEST_CASE("highlights resolve defaults and reverse video") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {
    A({"default_colors_set", A({0xeeeeee, 0x111111, 0xff0000, 0, 0})}),
    A({"hl_attr_define", A({3, Map{{"foreground", 0x00ff00}, {"bold", true}, {"undercurl", true}}, Map{}, A({})}),
      A({4, Map{{"reverse", true}}, Map{}, A({})})}),
    A({"hl_group_set", A({"Pmenu", 3})}),
  });
  CHECK(s.highlights.colors(3).fg == 0x00ff00);
  CHECK(s.highlights.colors(3).bg == 0x111111);
  CHECK(s.highlights.attr(3).bold);
  CHECK(s.highlights.attr(3).underline == ui::Underline::Curl);
  CHECK(s.highlights.colors(4).fg == 0x111111);
  CHECK(s.highlights.colors(4).bg == 0xeeeeee);
  CHECK(s.highlights.colors(0).fg == 0xeeeeee);  // id 0 is the default attribute
  CHECK(s.highlights.group("Pmenu") == 3);
}

TEST_CASE("multigrid placements: splits, floats, hide and close") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {
    A({"grid_resize", A({2, 40, 10}), A({3, 20, 5})}),
    A({"win_pos", A({2, 1000, 1, 0, 40, 10})}),
    A({"win_float_pos", A({3, 1001, "NW", 2, 2.0, 3.0, true, 50, 7, 4, 6})}),
  });
  CHECK(s.placements.at(2).role == ui::GridRole::Window);
  CHECK(s.placements.at(2).row == 1);
  const ui::Placement& f = s.placements.at(3);
  CHECK(f.role == ui::GridRole::Float);
  CHECK(f.row == 4);
  CHECK(f.col == 6);
  CHECK(f.width == 20);
  CHECK(f.zindex == 50);
  CHECK(f.compindex == 7);

  redraw(d, s, {A({"win_hide", A({3})})});
  CHECK_FALSE(s.placements.at(3).visible);
  redraw(d, s, {A({"win_close", A({3})}), A({"grid_destroy", A({3})})});
  CHECK(s.placements.count(3) == 0);
  CHECK(s.grids.count(3) == 0);
}

TEST_CASE("cmdline levels, messages and the popupmenu") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {
    define_hl(12),
    A({"cmdline_show", A({A({A({0, "echo", 0}), A({9, " 1", 9})}), 6, ":", "", 0, 1, 0})}),
    A({"msg_show", A({"emsg", A({A({12, "E492: Not an editor command", 33})}), false, true, false, 1, ""})}),
    A({"popupmenu_show", A({A({A({"foo", "f", "", ""}), A({"bar", "", "", ""})}), 0, 3, 4, 2})}),
  });
  REQUIRE(s.cmdline.visible());
  CHECK(s.cmdline.levels[0].firstc == ":");
  CHECK(s.cmdline.levels[0].content[1].hl == 9);
  REQUIRE(s.messages.visible.size() == 1);
  CHECK(s.messages.visible[0].kind == ui::MessageKind::Emsg);
  CHECK(s.messages.visible[0].is_error());
  CHECK(s.messages.unread.size() == 1);
  CHECK(s.popupmenu.visible);
  CHECK(s.popupmenu.items[1].word == "bar");

  redraw(d, s, {
    A({"msg_show", A({"emsg", A({A({12, "replaced", 33})}), false, true, false, 1, ""})}),
    A({"cmdline_hide", A({1, false})}),
    A({"popupmenu_select", A({1})}),
  });
  CHECK(s.messages.visible.size() == 1);  // same id replaces
  CHECK(s.messages.visible[0].content[0].text == "replaced");
  CHECK_FALSE(s.cmdline.visible());
  CHECK(s.popupmenu.selected == 1);
}

TEST_CASE("message kinds are classified once, at the boundary") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {
    A({"msg_show", A({"lua_error", A({}), false, true, false, 1, ""}), A({"wmsg", A({}), false, false, false, 2, ""}),
      A({"a_kind_from_the_future", A({}), false, false, false, "named", "typed_cmd"})}),
  });
  REQUIRE(s.messages.unread.size() == 3);
  CHECK(s.messages.unread[0].kind == ui::MessageKind::LuaError);
  CHECK(s.messages.unread[0].is_error());
  CHECK(s.messages.unread[1].kind == ui::MessageKind::Other);
  CHECK_FALSE(s.messages.unread[1].is_error());
  CHECK(s.messages.unread[2].kind == ui::MessageKind::Other);
  CHECK(s.messages.unread[2].id == "named");
  CHECK(s.messages.unread[2].trigger == "typed_cmd");
}

TEST_CASE("malformed tuples are skipped without losing the rest of the batch") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  d.apply(s, Array{
    A({"grid_resize", A({1, "wide", 2}), A({1, 4, 2})}),
    A({"grid_cursor_goto", A({1})}),
    Value("not an event"),
    A({}),
    A({"some_future_event", A({1, 2, 3})}),
    A({"set_title", A({"ok"})}),
    A({"flush", A({})}),
  });
  CHECK(s.grids.at(1).width() == 4);
  CHECK(s.title == "ok");
}

TEST_CASE("arguments outside their grid or table are rejected at the boundary") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {A({"grid_resize", A({1, 4, 2})}), A({"grid_line", A({1, 0, 0, A({A({"a", 0, 4})}), false})})});
  const uint64_t row_id = s.grids.at(1).row_id(0);

  redraw(d, s, {
    A({"grid_line",
      A({1, 0, -1, A({A({"b", 0})}), false}),             // column before the row
      A({1, 0, 4, A({A({"b", 0})}), false}),              // column past the row
      A({1, 2, 0, A({A({"b", 0})}), false}),              // row past the grid
      A({1, 0, 2, A({A({"b", 0, 2}), A({"c"})}), false}),   // runs past the end: nothing is written
      A({1, 0, 0, A({A({"b", 99})}), false}),             // undefined highlight
      A({1, 0, 0, A({A({"b"})}), false}),                 // first cell without an hl_id
      A({7, 0, 0, A({A({"b", 0})}), false})}),            // unknown grid
    A({"grid_scroll",
      A({1, 0, 3, 0, 4, 1, 0}),   // region below the grid
      A({1, 0, 2, 0, 4, 0, 0}),   // no move
      A({1, 0, 2, 0, 4, 3, 0}),   // move larger than the region
      A({8, 0, 1, 0, 1, 1, 0})}), // unknown grid
    A({"grid_resize", A({9, -1, 2}), A({9, 3, 100000})}),
    A({"hl_attr_define", A({-1, Map{}, Map{}, A({})}), A({4294967295LL, Map{}, Map{}, A({})})}),
    A({"grid_cursor_goto", A({1, 5, 0})}),
  });
  CHECK(row_text(s.grids.at(1), 0) == "aaaa");
  CHECK(s.grids.at(1).row_id(0) == row_id);
  CHECK(s.grids.size() == 1);  // nothing was created for unknown or invalid grids
  CHECK(s.highlights.size() == 1);
  CHECK(s.cursor.row == 0);
}

TEST_CASE("a malformed tuple leaves the state it would have replaced untouched") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {
    define_hl(1),
    A({"mode_info_set", A({true, A({Map{{"cursor_shape", "vertical"}}, Map{{"cursor_shape", "horizontal"}}})})}),
    A({"popupmenu_show", A({A({A({"one", "", "", ""}), A({"two", "", "", ""})}), 1, 0, 0, 1})}),
    A({"cmdline_block_show", A({A({A({A({0, "first", 0})})})})}),
    A({"msg_history_show", A({A({A({"echo", A({A({0, "old", 0})}), false})}), false})}),
  });
  const uint64_t history_generation = s.messages.history_generation;

  redraw(d, s, {
    A({"mode_info_set", A({true, A({Map{{"cursor_shape", "block"}}, Map{{"attr_id", 99}}})})}),
    A({"popupmenu_show", A({A({A({"three", "", "", ""}), A({"four"})}), 0, 0, 0, 1})}),
    A({"cmdline_block_show", A({A({A({A({0, "second", 0})}), A({A({99, "undefined highlight", 0})})})})}),
    A({"msg_history_show", A({A({A({"echo", A({A({0, "new", 0})}), false}), A({"echo", A({}), "not a bool"})}), false})}),
  });
  REQUIRE(s.modes.size() == 2);
  CHECK(s.modes[0].shape == ui::CursorShape::Vertical);
  REQUIRE(s.popupmenu.items.size() == 2);
  CHECK(s.popupmenu.items[0].word == "one");
  CHECK(s.popupmenu.selected == 1);
  REQUIRE(s.cmdline.block.size() == 1);
  CHECK(s.cmdline.block[0][0].text == "first");
  REQUIRE(s.messages.history.size() == 1);
  CHECK(s.messages.history[0].content[0].text == "old");
  CHECK(s.messages.history_generation == history_generation);
}

TEST_CASE("a redraw becomes visible only when its flush arrives") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  d.apply(s, Array{A({"grid_resize", A({1, 3, 1})})});
  d.apply(s, Array{A({"grid_line", A({1, 0, 0, A({A({"x", 0, 3})}), false})})});
  CHECK(s.grids.empty());
  CHECK_FALSE(s.changes.flushed);

  d.apply(s, Array{A({"flush", A({})})});
  REQUIRE(s.grids.count(1) == 1);
  CHECK(row_text(s.grids.at(1), 0) == "xxx");
  CHECK(s.changes.flushed);
}

TEST_CASE("an event has one handler") {
  ui::RedrawDispatcher d;
  d.add("bell", [](ui::UiState&, const Array&) {});
  CHECK_THROWS_AS(d.add("bell", [](ui::UiState&, const Array&) {}), std::logic_error);
}

TEST_CASE("viewport moves carry the rows their scroll pushed out") {
  ui::UiState s;
  auto d = ui::make_redraw_dispatcher();
  redraw(d, s, {
    A({"grid_resize", A({2, 3, 4})}),
    A({"grid_line", A({2, 0, 0, A({A({"a", 0, 3})}), false}), A({2, 1, 0, A({A({"b", 0, 3})}), false}),
      A({2, 2, 0, A({A({"c", 0, 3})}), false}), A({2, 3, 0, A({A({"d", 0, 3})}), false})}),
    A({"win_pos", A({2, 1000, 0, 0, 3, 4})}),
  });
  // Scroll down two lines: rows "a" and "b" leave off the top.
  redraw(d, s, {
    A({"grid_scroll", A({2, 0, 4, 0, 3, 1, 0})}),
    A({"grid_scroll", A({2, 0, 4, 0, 3, 1, 0})}),
    A({"win_viewport", A({2, 1000, 2, 6, 2, 0, 10, 2})}),
  });
  const ui::Viewport& v = s.placements.at(2).viewport;
  CHECK(v.scroll_seq == 1);
  REQUIRE(v.scrolled_out.size() == 2);
  CHECK(v.scrolled_out[0][0].text == "a");
  CHECK(v.scrolled_out[1][0].text == "b");

  // Inserting a line scrolls part of the window without moving the viewport.
  redraw(d, s, {
    A({"grid_scroll", A({2, 1, 4, 0, 3, -1, 0})}),
    A({"flush", A({})}),
    A({"grid_scroll", A({2, 0, 4, 0, 3, 1, 0})}),
    A({"win_viewport", A({2, 1000, 3, 7, 3, 0, 10, 1})}),
  });
  CHECK(v.scroll_seq == 2);
  CHECK(v.scrolled_out.size() == 1);  // only this batch's scroll, not the insert's
}
