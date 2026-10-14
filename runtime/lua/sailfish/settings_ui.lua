-- :Sailfish settings — every feature and option, editable in place. Changes
-- are written to the managed settings file, which hot-reloads the editor;
-- the screen reopens afterwards. Options set by the config's own code are
-- marked, since that code would override an edit made here.
local commands = require("sailfish.commands")
local features = require("sailfish.features")
local panel = require("sailfish.ui.panel")
local settings = require("sailfish.settings")
local state = require("sailfish.state")

local M = {}

local function next_choice(choices, current)
  for i, choice in ipairs(choices) do
    if choice == current then
      return choices[i % #choices + 1]
    end
  end
  return choices[1]
end

local function build_lines()
  local lines = { { { "" } } }
  for _, entry in ipairs(features.list()) do
    local on = entry.opts.enabled
    table.insert(lines, {
      { "  " },
      { on and "● on  " or "○ off ", on and "DiagnosticOk" or "Comment" },
      { string.format("%-12s", entry.name), "Title" },
      { entry.feature.description, "Normal" },
      item = { feature = entry, key = "enabled" },
    })
    -- Features with a screen of their own (plugins → the shop) list nothing here.
    if entry.feature.managed_by then
      table.insert(lines, {
        { "          " },
        { "managed in :Sailfish " .. entry.feature.managed_by, "Comment" },
        item = { open = entry.feature.managed_by },
      })
    else
      local keys = vim.tbl_filter(function(k)
        return k ~= "enabled"
      end, vim.tbl_keys(entry.feature.defaults))
      table.sort(keys)
      for _, key in ipairs(keys) do
        local overridden = vim.tbl_contains(entry.overridden, key)
        table.insert(lines, {
          { "          " },
          { key, "Identifier" },
          { " = " .. vim.inspect(entry.opts[key]), "Normal" },
          { overridden and "   (set in your config's code)" or "", "Comment" },
          item = { feature = entry, key = key },
        })
      end
    end
    table.insert(lines, { { "" } })
  end
  return lines
end

-- Asks for a new value; a number option only takes a number.
local function ask(label, current, commit)
  vim.ui.input({ prompt = label .. " = ", default = tostring(current) }, function(input)
    if input == nil then
      return
    end
    local value = input
    if type(current) == "number" then
      value = tonumber(input)
    end
    if value == nil then
      vim.notify("Sailfish: " .. label .. " needs a number", vim.log.levels.WARN)
    elseif value ~= current then
      commit(value)
    end
  end)
end

local function edit(target)
  if target.open then
    commands.run(target.open)
    return
  end
  local entry, key = target.feature, target.key
  local current = entry.opts[key]
  local function commit(value)
    -- The reload this triggers replaces the editor; come back to this screen.
    state.reopen_after_reload("settings")
    settings.set({ entry.name, key }, value)
  end
  local choices = entry.feature.choices and entry.feature.choices[key]
  if choices then
    commit(next_choice(choices, current))
  elseif type(current) == "boolean" then
    commit(not current)
  else
    ask(entry.name .. "." .. key, current, commit)
  end
end

function M.open()
  panel.open({
    title = "Sailfish settings — " .. vim.fn.fnamemodify(settings.file_path(), ":~"),
    lines = build_lines(),
    keys = {
      ["<CR>"] = {
        desc = "change",
        fn = function(_, target)
          if target then
            edit(target)
          end
        end,
      },
      e = {
        desc = "edit the settings file",
        fn = function(p)
          p:close()
          vim.cmd("edit " .. vim.fn.fnameescape(settings.file_path()))
        end,
      },
    },
  })
end

return M
