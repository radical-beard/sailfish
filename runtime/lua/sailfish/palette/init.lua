-- The command palette (Cmd/Ctrl+Shift+P): every action in one fuzzy list —
-- Sailfish's commands, built-in editor actions, the config's described
-- keymaps (run by pressing the keys for you) and its user commands.
-- Plugins and configs add actions with register().
local commands = require("sailfish.commands")
local picker = require("sailfish.ui.picker")

local M = {}

---@alias sailfish.PaletteAction {title: string, run: string|fun(), hint?: string}

---@type sailfish.PaletteAction[]
local registered = {}

---@param action sailfish.PaletteAction
function M.register(action)
  vim.validate("action", action, "table")
  vim.validate("action.title", action.title, "string")
  vim.validate("action.run", action.run, { "string", "function" })
  vim.validate("action.hint", action.hint, "string", true)
  table.insert(registered, action)
end

local function runner(run)
  return type(run) == "string" and function()
    vim.cmd(run)
  end or run
end

local function collect()
  local items, seen = {}, {}
  local function add(text, hint, run)
    if not seen[text] then
      seen[text] = true
      items[#items + 1] = { text = text, hint = hint, value = run }
    end
  end

  for _, subcommand in ipairs(commands.list()) do
    add("Sailfish: " .. subcommand.desc:gsub("^%l", string.upper), ":Sailfish " .. subcommand.name, function()
      commands.run(subcommand.name)
    end)
  end
  for _, action in ipairs(vim.list_extend(vim.deepcopy(require("sailfish.palette.builtin")), registered)) do
    add(action.title, action.hint, runner(action.run))
  end

  -- Described keymaps, the config's and its plugins'.
  local maps = vim.list_extend(vim.api.nvim_get_keymap("n"), vim.api.nvim_buf_get_keymap(0, "n"))
  for _, m in ipairs(maps) do
    if m.desc and m.desc ~= "" and not m.lhs:find("<Plug>", 1, true) then
      add(m.desc, vim.fn.keytrans(m.lhsraw), function()
        vim.api.nvim_feedkeys(vim.keycode(m.lhs), "m", false)
      end)
    end
  end

  local user_commands = vim.api.nvim_get_commands({ builtin = false })
  local command_names = vim.tbl_keys(user_commands)
  table.sort(command_names)
  for _, name in ipairs(command_names) do
    local cmd = user_commands[name]
    add(":" .. name, cmd.definition ~= "" and cmd.definition:sub(1, 50) or nil, function()
      if cmd.nargs == "0" then
        vim.cmd(name)
      else
        vim.api.nvim_feedkeys(":" .. name .. " ", "n", false)
      end
    end)
  end
  return items
end

function M.open()
  picker.open({
    title = "Command palette",
    items = collect(),
    on_pick = function(item)
      item.value()
    end,
  })
end

return M
