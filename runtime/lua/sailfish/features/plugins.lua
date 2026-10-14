-- Catalog plugins, installed and set up on request:
--
--   require("sailfish").setup({ plugins = { harpoon = true, lualine = false } })
--
-- or interactively with :Sailfish shop. Each plugin is true, false or "auto";
-- the essentials default to "auto", which means on for a fresh config and off
-- for one that brings its own setup. A plugin the config already provides
-- (through lazy.nvim or anything else) is never installed twice.
local catalog = require("sailfish.registry.catalog")
local config = require("sailfish.config")
local installer = require("sailfish.plugins.installer")

local by_id = {}
local defaults = { enabled = true }
local choices = {}
for _, entry in ipairs(catalog) do
  by_id[entry.id] = entry
  defaults[entry.id] = entry.essential and "auto" or false
  choices[entry.id] = { "auto", true, false }
end

local M = {
  description = "Curated plugins, installed and set up for you (:Sailfish shop)",
  order = 20,
  defaults = defaults,
  choices = choices,
  managed_by = "shop",
}

---@type table<string, "provided"|"installing"|"loaded"|"failed">
local status = {}

--- Where a catalog plugin stands in this editor; nil when it isn't wanted.
---@param id string
---@return "provided"|"installing"|"loaded"|"failed"|nil
function M.status(id)
  return status[id]
end

--- Whether the plugins feature's options turn a plugin on.
---@param id string
---@param opts table the plugins feature's options
function M.enabled(id, opts)
  local value = opts[id]
  return value == true or (value == "auto" and config.fresh())
end

local function module_on_runtimepath(module)
  local base = "lua/" .. module:gsub("%.", "/")
  return #vim.api.nvim_get_runtime_file(base .. ".lua", false) > 0 or #vim.api.nvim_get_runtime_file(base .. "/init.lua", false) > 0
end

-- Whether the config already has this plugin: lazy.nvim manages it (loaded
-- or not yet), or its module is on 'runtimepath' some other way. Asked once,
-- before Sailfish loads anything (its own copies are :packadd-ed later).
local function provided_by_config(entry)
  local ok, lazy = pcall(require, "lazy.core.config")
  if ok and type(lazy.plugins) == "table" then
    for name, plugin in pairs(lazy.plugins) do
      if name == vim.fs.basename(entry.repo) or plugin[1] == entry.repo or (plugin.url or ""):find(entry.repo, 1, true) then
        return true
      end
    end
  end
  return entry.module ~= nil and module_on_runtimepath(entry.module)
end

-- Enabled entries plus everything they depend on, dependencies first.
local function wanted(opts)
  local order, seen = {}, {}
  local function visit(entry)
    if seen[entry.id] then
      return
    end
    seen[entry.id] = true
    for _, dep in ipairs(entry.depends or {}) do
      visit(by_id[dep])
    end
    table.insert(order, entry)
  end
  for _, entry in ipairs(catalog) do
    if M.enabled(entry.id, opts) then
      visit(entry)
    end
  end
  return order
end

-- Loading runs the plugin's own plugin/ scripts, so it's guarded like setup.
local function set_up(entry)
  local ok, err = xpcall(function()
    installer.load(entry.id)
    if entry.setup then
      entry.setup()
    end
  end, debug.traceback)
  status[entry.id] = ok and "loaded" or "failed"
  if not ok then
    vim.notify("Sailfish: setting up " .. entry.name .. " failed:\n" .. err, vim.log.levels.ERROR)
  end
end

local function load_all(entries)
  for _, entry in ipairs(entries) do
    if status[entry.id] ~= "failed" then
      set_up(entry)
    end
  end
  -- Other features (languages) may be waiting on these plugins.
  vim.api.nvim_exec_autocmds("User", { pattern = "SailfishPluginsReady", modeline = false })
end

-- Installs or updates `entries` in parallel, then calls `done`. A plugin
-- that fails to update keeps the checkout it has; one that fails to install
-- is left out.
local function install(entries, done)
  local names = vim.tbl_map(function(e)
    return e.name
  end, entries)
  vim.api.nvim_echo({ { "Sailfish: installing " .. table.concat(names, ", ") .. "…", "MoreMsg" } }, false, {})
  local remaining = #entries
  for _, entry in ipairs(entries) do
    status[entry.id] = "installing"
    installer.install(entry, function(ok, err)
      if not ok and installer.installed(entry.id) then
        vim.notify("Sailfish: updating " .. entry.name .. " failed, keeping the installed version: " .. err, vim.log.levels.WARN)
      elseif not ok then
        status[entry.id] = "failed"
        vim.notify("Sailfish: installing " .. entry.name .. " failed: " .. err, vim.log.levels.ERROR)
      end
      remaining = remaining - 1
      if remaining == 0 then
        done()
        vim.api.nvim_echo({ { "Sailfish: plugins ready", "MoreMsg" } }, false, {})
      end
    end)
  end
end

function M.start(opts)
  if config.fresh() and vim.g.mapleader == nil then
    vim.g.mapleader = " "
  end
  local ours = {}
  for _, entry in ipairs(wanted(opts)) do
    if provided_by_config(entry) then
      status[entry.id] = "provided"
    else
      table.insert(ours, entry)
    end
  end
  local outdated = vim.tbl_filter(function(entry)
    return not installer.current(entry)
  end, ours)
  if #outdated == 0 then
    return load_all(ours)
  end
  install(outdated, function()
    load_all(ours)
  end)
end

return M
