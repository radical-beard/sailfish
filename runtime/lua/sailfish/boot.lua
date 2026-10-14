-- Entry point, run by the host with `--cmd` before the user's config loads.
--
-- Sailfish modules resolve through a package loader rather than
-- 'runtimepath', because plugin managers rewrite 'runtimepath' (lazy.nvim
-- resets it) and Sailfish must keep working whatever the config does.
local root = vim.g.sailfish.runtime
local lua_dir = root .. "/lua/"

table.insert(package.loaders, 2, function(name)
  if name ~= "sailfish" and not vim.startswith(name, "sailfish.") then
    return nil
  end
  local path = lua_dir .. name:gsub("%.", "/")
  for _, file in ipairs({ path .. ".lua", path .. "/init.lua" }) do
    if vim.uv.fs_stat(file) then
      return assert(loadfile(file))
    end
  end
  return "\n\tno Sailfish runtime module '" .. name .. "'"
end)

-- Help pages and queries still come from 'runtimepath'.
vim.opt.runtimepath:append(root)

require("sailfish.core").boot()
