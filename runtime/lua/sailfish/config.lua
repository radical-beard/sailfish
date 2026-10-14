-- Where the user's config lives. Edits and the file watcher use the live
-- config; in safe mode Neovim runs the last working snapshot of it instead,
-- and whatever describes the running config reads that one.
local M = {}

--- The live config directory.
function M.root()
  local root = vim.g.sailfish.config_root or vim.fn.stdpath("config")
  return vim.uv.fs_realpath(root) or root
end

--- The config directory this Neovim loaded: the live one, or in safe mode
--- the snapshot.
function M.loaded()
  return vim.fn.stdpath("config")
end

--- A config with no init.lua or init.vim brings no setup of its own.
function M.fresh()
  local dir = M.loaded()
  return not (vim.uv.fs_stat(dir .. "/init.lua") or vim.uv.fs_stat(dir .. "/init.vim"))
end

return M
