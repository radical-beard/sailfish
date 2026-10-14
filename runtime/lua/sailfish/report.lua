-- Config error reports and reload notices, shown in the active instance on
-- the host's behalf. Errors that name a file and line can be opened
-- directly; actions (reload, load the last working config) go back to the host.
local host = require("sailfish.host")
local location = require("sailfish.ui.location")
local panel = require("sailfish.ui.panel")

local M = {}

-- Lua shortens long chunk names to "...tail/of/path.lua"; find the file in
-- the config that ends that way.
local function resolve(file, config_root)
  if not vim.startswith(file, "...") then
    return file
  end
  local tail = file:sub(4)
  local found = vim.fs.find(function(name, dir)
    return vim.endswith(dir .. "/" .. name, tail)
  end, { path = config_root, type = "file", limit = 1 })
  return found[1] or file
end

---@return {file: string, line: integer}?
function M.locate(text, config_root)
  -- Lua errors: "path/file.lua:12: message"
  local file, line = text:match("([^%s:]+%.lua):(%d+):")
  if not file then
    -- Vimscript errors: "Error in path/file.vim:\nline   12:"
    file = text:match("Error in ([^\n:]+):")
    line = text:match("line%s+(%d+):")
  end
  if not (file and line) then
    return nil
  end
  return { file = resolve(file, config_root), line = tonumber(line) }
end

---@param report {title: string, errors: string[], actions: {key: string, label: string, action: string}[], config_root: string}
function M.show(report)
  vim.validate("report.title", report.title, "string")
  vim.validate("report.errors", report.errors, vim.islist, "a list of strings")
  vim.validate("report.actions", report.actions, vim.islist, "a list of actions")
  vim.validate("report.config_root", report.config_root, "string")

  local lines = { { { "" } } }
  for i, err in ipairs(report.errors) do
    local site = M.locate(err, report.config_root)
    for j, text in ipairs(vim.split(err, "\n", { trimempty = true })) do
      local prefix = j == 1 and string.format(" %2d  ", i) or "     "
      table.insert(lines, { { prefix, "Comment" }, { text, j == 1 and "ErrorMsg" or "Normal" }, item = site })
    end
    if site then
      table.insert(lines, { { "     " }, { "→ " .. site.file .. ":" .. site.line, "Directory" }, item = site })
    end
    table.insert(lines, { { "" } })
  end
  if #report.errors == 0 then
    table.insert(lines, { { "  Nothing to show." , "Comment" } })
    table.insert(lines, { { "" } })
  end

  local keys = {
    ["<CR>"] = {
      desc = "open error location",
      fn = function(p, site)
        if site then
          p:close()
          location.open(site)
        end
      end,
    },
  }
  for _, action in ipairs(report.actions) do
    keys[action.key] = {
      desc = action.label,
      fn = function(p)
        p:close()
        host.notify("config.action", action.action)
      end,
    }
  end
  panel.open({ title = "  " .. report.title, lines = lines, keys = keys, wrap = true })
end

---@param info {safe: boolean, version?: string, time?: string}
function M.reloaded(info)
  vim.validate("info.safe", info.safe, "boolean")
  if info.safe then
    -- Safe mode always runs a recorded version, which has a time.
    vim.validate("info.time", info.time, "string")
    vim.api.nvim_echo({
      { "Running your last working config (" .. info.time .. "). ", "WarningMsg" },
      { "Fix your config and save it to leave safe mode.", "Normal" },
    }, true, {})
  else
    vim.api.nvim_echo({ { "Config reloaded", "MoreMsg" } }, false, {})
  end
end

return M
