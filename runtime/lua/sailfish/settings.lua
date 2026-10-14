-- Sailfish's settings, layered (later wins):
--   1. each feature's defaults
--   2. the managed settings file, sailfish.lua in the config, which the
--      settings screen rewrites (plain Lua returning a table)
--   3. whatever the config's own code passes to require("sailfish").setup()
-- Every value is checked against its feature's defaults and choices when the
-- options are resolved. Changing either file reloads the editor like any
-- other config change.
local config = require("sailfish.config")

local M = {}

local kFileName = "sailfish.lua"
local kSetup = "require('sailfish').setup()"

---@type table<string, table>
local from_code = {}

--- The live settings file, which edits go to.
function M.file_path()
  return config.root() .. "/" .. kFileName
end

-- Settings from one source: feature name → options.
local function check_shape(values, source)
  if type(values) ~= "table" then
    error(source .. " must give a table of feature settings", 0)
  end
  for name, options in pairs(values) do
    if type(name) ~= "string" or type(options) ~= "table" then
      error(("%s: '%s' must be a table of options"):format(source, tostring(name)), 0)
    end
  end
  return values
end

local function read(path)
  if not vim.uv.fs_stat(path) then
    return {}
  end
  return check_shape(assert(loadfile(path))(), path)
end

---@param opts table<string, table> per-feature options
function M.add_from_code(opts)
  from_code = vim.tbl_deep_extend("force", from_code, check_shape(opts, kSetup))
end

---@class sailfish.SettingsFile
---@field path string
---@field options table<string, table>

--- The settings file this Neovim loaded: in safe mode, the snapshot's.
---@return sailfish.SettingsFile
function M.load()
  local path = config.loaded() .. "/" .. kFileName
  return { path = path, options = read(path) }
end

--- Feature names the file or setup() mention that no feature registered.
---@param file sailfish.SettingsFile
---@param features table<string, table>
---@return string[]
function M.unknown_features(file, features)
  local unknown = {}
  for _, values in ipairs({ file.options, from_code }) do
    for name in pairs(values) do
      if not features[name] then
        unknown[name] = true
      end
    end
  end
  return vim.tbl_keys(unknown)
end

-- Every option a source sets must exist and fit its default's type, or one
-- of its choices.
local function check(source, name, feature, options)
  for key, value in pairs(options) do
    local default = feature.defaults[key]
    local allowed = feature.choices and feature.choices[key]
    local problem
    if default == nil then
      problem = "is not an option"
    elseif allowed and not vim.tbl_contains(allowed, value) then
      problem = "must be one of " .. table.concat(vim.tbl_map(vim.inspect, allowed), ", ")
    elseif not allowed and type(value) ~= type(default) then
      problem = "must be a " .. type(default)
    end
    if problem then
      error(("%s: %s.%s %s (got %s)"):format(source, name, key, problem, vim.inspect(value)), 0)
    end
  end
end

--- A feature's effective options, and the keys the config's code sets.
---@param file sailfish.SettingsFile
---@return table effective, string[] overridden
function M.resolve(name, feature, file)
  local from_file, code = file.options[name] or {}, from_code[name] or {}
  check(file.path, name, feature, from_file)
  check(kSetup, name, feature, code)
  return vim.tbl_extend("force", feature.defaults, from_file, code), vim.tbl_keys(code)
end

-- Serializes plain data (tables of strings, numbers, booleans) as Lua source.
local function serialize(value, indent)
  if type(value) ~= "table" then
    return vim.inspect(value)
  end
  local keys = vim.tbl_keys(value)
  table.sort(keys, function(a, b)
    return tostring(a) < tostring(b)
  end)
  local pad = string.rep("  ", indent + 1)
  local lines = { "{" }
  for _, key in ipairs(keys) do
    local name = type(key) == "string" and key:match("^[%a_][%w_]*$") and key or ("[" .. vim.inspect(key) .. "]")
    table.insert(lines, pad .. name .. " = " .. serialize(value[key], indent + 1) .. ",")
  end
  table.insert(lines, string.rep("  ", indent) .. "}")
  return table.concat(lines, "\n")
end

-- Sets one value in the live settings file, e.g. set({"autosave", "enabled"}, true).
---@param path string[]
function M.set(path, value)
  local data = read(M.file_path())
  local node = data
  for i = 1, #path - 1 do
    node[path[i]] = node[path[i]] or {}
    node = node[path[i]]
  end
  node[path[#path]] = value
  local source = "-- Sailfish settings, rewritten by :Sailfish settings (comments are not kept).\n"
    .. "-- Values set by require('sailfish').setup() in your config take precedence.\n"
    .. "return "
    .. serialize(data, 0)
    .. "\n"
  -- A fresh setup may not have a config directory yet.
  vim.fn.mkdir(config.root(), "p")
  vim.fn.writefile(vim.split(source, "\n", { plain = true }), M.file_path())
  -- Written without a buffer, so tell the host directly rather than waiting
  -- for the file watcher.
  require("sailfish.host").notify("config.changed", { M.file_path() })
end

return M
