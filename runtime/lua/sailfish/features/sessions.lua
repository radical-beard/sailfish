-- Reopens each project (the working directory Sailfish started in) where it
-- was left: windows, tabs, buffers, cursors and folds. Sessions are saved on
-- exit and periodically, so even a crash loses little.
--
-- Hot exit (VS Code's term): closing the window with unsaved changes keeps
-- them, and they're back, still unsaved and with undo history, next time.
-- Quitting any other way (:qa!, discarding at the prompt) discards them.
local state = require("sailfish.state")

local M = {
  description = "Reopen each project where you left it, unsaved edits included",
  order = 10,  -- restore buffers before other features react to them
  defaults = { enabled = true, hot_exit = true, save_interval = 30 },
}

-- Session plugins (their Lua modules) whose presence means the config
-- restores sessions itself.
local kSessionPlugins = { "persistence", "auto-session", "resession", "possession", "persisted", "mini.sessions" }

local function session_dir(cwd)
  -- One directory per project; the path is flattened like Vim's swap names.
  return vim.g.sailfish.data_dir .. "/sessions/" .. cwd:gsub("[/\\:]", "%%")
end

-- The config loaded a session already (-S, or a plugin set v:this_session),
-- or uses a session plugin.
local function config_manages_sessions()
  if vim.v.this_session ~= "" or vim.tbl_contains(vim.v.argv, "-S") or vim.g.loaded_obsession then
    return true
  end
  for _, module in ipairs(kSessionPlugins) do
    if package.loaded[module] then
      return true
    end
  end
  return false
end

function M.start(opts)
  local dir = session_dir(vim.fn.getcwd())
  local closed_by_hot_exit = false

  ---@param unsaved "keep"|"drop"
  local function save(unsaved)
    -- An instance being replaced by a hot reload must not overwrite the
    -- session its successor now owns.
    if not vim.g.sailfish_retiring then
      state.export(dir, { unsaved = unsaved })
    end
  end

  local group = vim.api.nvim_create_augroup("sailfish.sessions", { clear = true })
  vim.api.nvim_create_autocmd("VimLeavePre", {
    group = group,
    callback = function()
      if not closed_by_hot_exit then
        save("drop")
      end
    end,
  })
  local periodic = opts.hot_exit and "keep" or "drop"
  local timer = assert(vim.uv.new_timer())
  timer:start(opts.save_interval * 1000, opts.save_interval * 1000, vim.schedule_wrap(function()
    -- Don't snapshot halfway through a command or prompt.
    if vim.api.nvim_get_mode().mode == "n" then
      save(periodic)
    end
  end))

  if opts.hot_exit then
    require("sailfish.core").on_window_close(function()
      -- A session can't carry every edit (a plugin's acwrite buffer); those
      -- get the usual questions.
      if state.has_uncarried_edits() then
        vim.cmd("confirm qall")
        return
      end
      save("keep")
      closed_by_hot_exit = true
      vim.cmd("qall!")
    end)
  end

  -- Restored last: if the saved session won't restore, the saves set up
  -- above still replace it.
  local launched_with_files = vim.fn.argc() > 0
  local reloading = vim.g.sailfish.import ~= nil
  if not launched_with_files and not reloading and not config_manages_sessions() and vim.uv.fs_stat(dir .. "/state.json") then
    state.import(dir)
  end
end

return M
