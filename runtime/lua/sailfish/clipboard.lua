-- System clipboard through the host, so "+y and "+p work on every OS
-- without pbcopy, xclip or win32yank. A config that sets g:clipboard
-- itself still wins: this runs first.
local host = require("sailfish.host")

local M = {}

local function copy(lines, regtype)
  host.notify("clipboard.set", lines, regtype)
end

local function paste()
  return host.request("clipboard.get")
end

function M.install()
  vim.g.clipboard = {
    name = "sailfish",
    copy = { ["+"] = copy, ["*"] = copy },
    paste = { ["+"] = paste, ["*"] = paste },
    cache_enabled = 0,
  }
end

return M
