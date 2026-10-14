-- Tells the host when the user's config changes, which triggers a hot
-- reload. Saves from inside Sailfish are seen immediately; edits from
-- elsewhere (git pull, terminal Neovim) through a file watcher. Changes are
-- batched briefly so a multi-file save reloads once.
local config = require("sailfish.config")
local host = require("sailfish.host")

local M = {}

local kDebounceMs = 250

-- Rewritten by plugin managers on their own; they don't change behavior
-- until the next reload anyway.
local ignored_names = {
  ["lazy-lock.json"] = true,
  ["nvim-pack-lock.json"] = true,
  [".DS_Store"] = true,
}

local function ignored(file)
  local name = vim.fs.basename(file)
  return ignored_names[name] or name:match("%.sw.$") or name:match("~$") or file:match("/%.git/") ~= nil
end

function M.start()
  local root = config.root()
  local pending = {}
  local timer = assert(vim.uv.new_timer())

  local function flush()
    local files = vim.tbl_keys(pending)
    pending = {}
    if #files > 0 then
      host.notify("config.changed", files)
    end
  end

  local function changed(file)
    if ignored(file) then
      return
    end
    pending[file] = true
    timer:stop()
    timer:start(kDebounceMs, 0, vim.schedule_wrap(flush))
  end

  vim.api.nvim_create_autocmd("BufWritePost", {
    group = vim.api.nvim_create_augroup("sailfish.config_watch", { clear = true }),
    callback = function(ev)
      local file = vim.uv.fs_realpath(ev.match)
      if file and vim.startswith(file, root .. "/") then
        changed(file)
      end
    end,
  })

  -- Recursive on macOS and Windows; Linux sees top-level changes only, and
  -- in-editor saves are covered by the autocmd above either way. A watcher
  -- that fails stays stopped, and says so once; a config directory that
  -- doesn't exist yet has nothing to watch.
  local function stopped(err)
    vim.notify("Sailfish: not watching " .. root .. " (" .. err .. "); edits made outside Sailfish won't reload the config", vim.log.levels.WARN)
  end
  local watcher = assert(vim.uv.new_fs_event())
  local ok, err = watcher:start(root, { recursive = true }, function(event_err, name)
    if event_err then
      watcher:stop()
      vim.schedule(function()
        stopped(event_err)
      end)
    elseif name then
      vim.schedule(function()
        changed(root .. "/" .. name)
      end)
    end
  end)
  if not ok then
    watcher:close()
    if vim.uv.fs_stat(root) then
      stopped(err)
    end
  end
end

return M
