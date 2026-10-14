-- :Sailfish history — every recorded version of the config, whether it
-- started cleanly, and what changed. Restoring a version rewrites the config
-- files (after recording the current state), which reloads the editor.
local host = require("sailfish.host")
local panel = require("sailfish.ui.panel")
local scratch = require("sailfish.ui.scratch")

local M = {}

local status_style = {
  good = { "● ", "DiagnosticOk" },
  bad = { "✗ ", "DiagnosticError" },
}

local function show_diff(version)
  local buf = scratch.create("sailfish://history/" .. version.hash:sub(1, 8), {
    filetype = "git",
    bufhidden = "wipe",
    lines = vim.split(host.request("config.show", version.hash), "\n", { plain = true }),
  })
  vim.cmd("tab sbuffer " .. buf)
end

function M.open()
  local versions = host.request("config.history", 100)
  local lines = { { { "" } } }
  for _, v in ipairs(versions) do
    -- A version without a status hasn't been started yet.
    local style = status_style[v.status] or { "○ ", "Comment" }
    table.insert(lines, {
      { "  " },
      style,
      { os.date("%b %d %H:%M  ", v.time), "Comment" },
      { string.format("%-40s", v.message), "Normal" },
      { v.hash:sub(1, 8), "Identifier" },
      item = v,
    })
  end
  if #versions == 0 then
    table.insert(lines, { { "  No versions recorded yet.", "Comment" } })
  end
  panel.open({
    title = "Config history",
    lines = lines,
    keys = {
      ["<CR>"] = {
        desc = "show changes",
        fn = function(p, version)
          if version then
            p:close()
            show_diff(version)
          end
        end,
      },
      R = {
        desc = "restore this version",
        fn = function(p, version)
          if not version then
            return
          end
          local answer = vim.fn.confirm("Restore the config to " .. version.hash:sub(1, 8) .. " (" .. version.message .. ")?", "&Restore\n&Cancel", 2)
          if answer == 1 then
            p:close()
            host.request("config.restore", version.hash)
          end
        end,
      },
    },
  })
end

return M
