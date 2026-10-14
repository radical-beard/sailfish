-- Saves files automatically, with VS Code's files.autoSave modes:
--   after_delay       a pause after editing (only outside insert mode, so
--                     write hooks such as whitespace trimming never fire
--                     mid-word)
--   on_focus_change   when the Sailfish window loses focus
--   on_window_change  when leaving a buffer or window, and on focus loss
-- A file that changed on disk since it was read or saved is left alone.
local M = {
  description = "Save files automatically (VS Code's files.autoSave)",
  defaults = { enabled = false, mode = "after_delay", delay = 1000 },
  choices = { mode = { "after_delay", "on_focus_change", "on_window_change" } },
}

-- mtime of each buffer's file as of our last read or write of it.
local known_mtime = {}
-- Buffers whose last autosave failed; each failure is reported once.
local failing = {}

local function mtime(buf)
  local stat = vim.uv.fs_stat(vim.api.nvim_buf_get_name(buf))
  return stat and stat.mtime.sec * 1e9 + stat.mtime.nsec
end

local function savable(buf)
  if not vim.api.nvim_buf_is_valid(buf) then
    return false
  end
  local bo = vim.bo[buf]
  if not (bo.modified and bo.modifiable and not bo.readonly and bo.buftype == "") then
    return false
  end
  local name = vim.api.nvim_buf_get_name(buf)
  if name == "" or vim.fn.isdirectory(vim.fs.dirname(name)) == 0 then
    return false
  end
  -- Never overwrite a newer version written by something else.
  local disk = mtime(buf)
  return disk == nil or disk == known_mtime[buf]
end

local function save(buf)
  if not savable(buf) then
    return
  end
  local ok, err = pcall(vim.api.nvim_buf_call, buf, function()
    vim.cmd("silent lockmarks update")
  end)
  if ok then
    failing[buf] = nil
  elseif not failing[buf] then
    failing[buf] = true
    vim.notify("Sailfish: autosave couldn't write " .. vim.api.nvim_buf_get_name(buf) .. ": " .. err, vim.log.levels.WARN)
  end
end

local function save_all()
  for _, buf in ipairs(vim.api.nvim_list_bufs()) do
    save(buf)
  end
end

-- Saves every buffer edited since the last save, once editing pauses in
-- normal mode.
local function save_after_delay(delay, on)
  local dirty = {}
  local timer = assert(vim.uv.new_timer())
  local arm
  local function flush()
    if vim.api.nvim_get_mode().mode ~= "n" then
      return arm()
    end
    for buf in pairs(dirty) do
      save(buf)
    end
    dirty = {}
  end
  arm = function()
    timer:stop()
    timer:start(delay, 0, vim.schedule_wrap(flush))
  end
  on({ "TextChanged", "InsertLeave" }, function(ev)
    dirty[ev.buf] = true
    arm()
  end)
end

function M.start(opts)
  local group = vim.api.nvim_create_augroup("sailfish.autosave", { clear = true })
  local function on(events, fn)
    vim.api.nvim_create_autocmd(events, { group = group, callback = fn })
  end

  -- Buffers read before autosave started (files opened at launch, a
  -- restored session) are known as of now.
  for _, buf in ipairs(vim.api.nvim_list_bufs()) do
    if vim.api.nvim_buf_is_loaded(buf) and vim.bo[buf].buftype == "" then
      known_mtime[buf] = mtime(buf)
    end
  end
  on({ "BufReadPost", "BufWritePost" }, function(ev)
    known_mtime[ev.buf] = mtime(ev.buf)
  end)
  on("BufWipeout", function(ev)
    known_mtime[ev.buf] = nil
    failing[ev.buf] = nil
  end)

  if opts.mode == "after_delay" then
    save_after_delay(opts.delay, on)
  else
    on("FocusLost", save_all)
    if opts.mode == "on_window_change" then
      on({ "BufLeave", "WinLeave" }, function(ev)
        save(ev.buf)
      end)
    end
  end
end

return M
