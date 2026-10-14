-- The public API, for configs running inside Sailfish. Guard it so the same
-- config still works in terminal Neovim:
--
--   if vim.g.sailfish then
--     require("sailfish").setup({
--       autosave = { enabled = true, mode = "on_focus_change" },
--       sessions = { hot_exit = false },
--     })
--   end
local M = {}

---@param opts table<string, table> per-feature options; see :Sailfish settings
function M.setup(opts)
  require("sailfish.settings").add_from_code(opts)
end

---@param name string
---@param feature table see lua/sailfish/features/init.lua
function M.register_feature(name, feature)
  require("sailfish.features").register(name, feature)
end

---@param name string
---@param subcommand sailfish.Subcommand see lua/sailfish/commands.lua
function M.register_command(name, subcommand)
  require("sailfish.commands").register(name, subcommand)
end

return M
