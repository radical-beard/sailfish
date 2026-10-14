-- Conveniences that only make sense in a window: font zoom, and default
-- shortcuts on chords a terminal can't even send (Cmd on macOS, Ctrl+Shift
-- elsewhere). A shortcut is only bound if the config hasn't used the keys.
local keys = require("sailfish.keys")

local M = {}

local initial_size

---@param step integer  +1 bigger, -1 smaller, 0 back to the starting size
function M.zoom(step)
  local font = vim.o.guifont
  local families, size, rest = font:match("^(.-):h([%d%.]+)(.*)$")
  if not families then
    families, size, rest = font, "13", ""
  end
  size = tonumber(size)
  initial_size = initial_size or size
  local new = step == 0 and initial_size or math.max(6, size + step)
  vim.o.guifont = families .. ":h" .. new .. rest
end

local function open_palette()
  vim.cmd("stopinsert")
  require("sailfish.palette").open()
end

function M.install_default_keys()
  local shortcuts = {
    { keys.chord("<D-S-p>", "<C-S-p>"), open_palette, "Command palette" },
    { keys.chord("<D-=>", "<C-=>"), function() M.zoom(1) end, "Zoom in" },
    { keys.chord("<D-->", "<C-->"), function() M.zoom(-1) end, "Zoom out" },
    { keys.chord("<D-0>", "<C-0>"), function() M.zoom(0) end, "Reset zoom" },
  }
  for _, shortcut in ipairs(shortcuts) do
    local lhs, rhs, desc = unpack(shortcut)
    keys.offer({ "n", "i", "v" }, lhs, rhs, desc)
  end
end

return M
