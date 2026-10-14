#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ui/grid.h"
#include "ui/highlight.h"

namespace sailfish::ui {

// ---- windows (ext_multigrid) ------------------------------------------------

enum class GridRole { Window, Float, External };

struct Viewport {
  int64_t scroll_delta = 0;
  // Bumped on every viewport move, with the rows that move scrolled away
  // (empty when Neovim redrew instead of scrolling, e.g. a long jump).
  uint64_t scroll_seq = 0;
  std::vector<std::vector<Cell>> scrolled_out;
};

struct Margins {
  int top = 0;
  int bottom = 0;
  int left = 0;
  int right = 0;
};

// Where a grid is composited onto the root grid, in cells.
struct Placement {
  GridRole role = GridRole::Window;
  int row = 0;
  int col = 0;
  int width = 0;
  int height = 0;
  int zindex = 0;
  int compindex = 0;
  bool visible = false;  // from win_pos / win_float_pos: viewport events can come first
  Viewport viewport;
  Margins margins;
};

// ---- cursor and modes -------------------------------------------------------

enum class CursorShape { Block, Horizontal, Vertical };

struct ModeInfo {
  CursorShape shape = CursorShape::Block;
  int cell_percentage = 100;
  int attr_id = 0;  // 0: swap the cell's foreground and background
  int blinkwait = 0;
  int blinkon = 0;
  int blinkoff = 0;
};

struct CursorPosition {
  int grid = 1;
  int row = 0;
  int col = 0;
};

// ---- externalized widgets ---------------------------------------------------

struct StyledText {
  int hl = 0;
  std::string text;
};
using StyledLine = std::vector<StyledText>;

struct CmdlineLevel {
  StyledLine content;
  int pos = 0;  // byte offset of the cursor in the concatenated content
  std::string firstc;
  std::string prompt;  // drawn in the default colors: cmdline_show's hl_id names a group, not an attribute
  int indent = 0;
  int level = 1;
  std::string special_char;  // pending c_CTRL-V style indicator, shown at pos
};

struct CmdlineState {
  std::vector<CmdlineLevel> levels;  // nested cmdlines, innermost last
  std::vector<StyledLine> block;     // cmdline_block_* context lines

  [[nodiscard]] bool visible() const { return !levels.empty(); }
};

// The msg_show kinds Sailfish treats specially. Neovim adds kinds over time;
// any kind not named here is Other.
enum class MessageKind { Other, Empty, Confirm, SearchCmd, SearchCount, Emsg, Echoerr, LuaError, RpcError };

struct Message {
  uint64_t seq = 0;  // increases with every msg_show, so layers can tell new from seen
  MessageKind kind = MessageKind::Other;
  StyledLine content;
  bool append = false;
  std::string id;  // integer ids are stored in decimal
  std::string trigger;  // "typed_cmd" when an interactively typed command produced it

  [[nodiscard]] bool is_error() const;
};

struct MessageState {
  std::vector<Message> visible;
  StyledLine showmode;
  StyledLine showcmd;
  StyledLine ruler;
  std::vector<Message> history;  // the last msg_history_show listing
  uint64_t history_generation = 0;  // bumped on every msg_history_show
  uint64_t generation = 0;  // bumped on every change to `visible`
  // Every msg_show since the host last looked, in order (error capture).
  std::vector<Message> unread;
};

struct PopupmenuItem {
  std::string word;
  std::string kind;
  std::string menu;
};

struct PopupmenuState {
  bool visible = false;
  std::vector<PopupmenuItem> items;
  int selected = -1;
  int row = 0;
  int col = 0;
  int grid = 0;  // -1: anchored to the external cmdline
};

// ---- options ------------------------------------------------------------------

struct UiOptions {
  std::string guifont;
  int linespace = 0;
};

// Facts the host reacts to once per frame; the consumer clears them.
struct Changes {
  bool flushed = false;
  bool title = false;
  bool font = false;
};

// Everything Neovim has told one UI client, as of its last flush. Redraw
// handlers write it; `messages.unread` and `changes` are mailboxes their
// consumers drain.
struct UiState {
  HighlightTable highlights;
  std::map<int, Grid> grids;
  std::map<int, Placement> placements;

  CursorPosition cursor;
  std::vector<ModeInfo> modes;
  int mode_index = 0;
  std::string mode;
  bool busy = false;

  UiOptions options;
  std::string title;

  CmdlineState cmdline;
  MessageState messages;
  PopupmenuState popupmenu;

  Changes changes;

  [[nodiscard]] const ModeInfo* current_mode() const;
};

} // namespace sailfish::ui
