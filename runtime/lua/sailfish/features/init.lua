-- The feature registry. Every module in lua/sailfish/features/ is a feature;
-- plugins and configs can add their own with register(). A feature is:
--
--   {
--     description = "one line for the settings screen",
--     defaults = { enabled = true, ... },   -- every option and its default
--     choices = { mode = { "a", "b" } },    -- optional: allowed values per option
--     order = 50,                           -- optional: start order, lower first
--     managed_by = "shop",                  -- optional: the :Sailfish screen for its options
--     start = function(opts) ... end,       -- runs after the config loads, if enabled
--   }
local settings = require("sailfish.settings")

local M = {}

---@type table<string, table>
local registered = {}

---@param name string
---@param feature table
function M.register(name, feature)
  vim.validate("name", name, "string")
  vim.validate("feature", feature, "table")
  vim.validate(name .. ".description", feature.description, "string")
  vim.validate(name .. ".defaults", feature.defaults, "table")
  vim.validate(name .. ".defaults.enabled", feature.defaults.enabled, "boolean")
  vim.validate(name .. ".start", feature.start, "function")
  vim.validate(name .. ".order", feature.order, "number", true)
  vim.validate(name .. ".choices", feature.choices, "table", true)
  vim.validate(name .. ".managed_by", feature.managed_by, "string", true)
  for key, allowed in pairs(feature.choices or {}) do
    if not vim.tbl_contains(allowed, feature.defaults[key]) then
      error(("feature '%s': the default of '%s' isn't one of its choices"):format(name, key), 2)
    end
  end
  registered[name] = feature
end

local function discover()
  local dir = vim.g.sailfish.runtime .. "/lua/sailfish/features"
  for file, kind in vim.fs.dir(dir) do
    local name = file:match("^(.-)%.lua$")
    if kind == "file" and name and name ~= "init" and not registered[name] then
      M.register(name, require("sailfish.features." .. name))
    end
  end
end

-- Names in start order: `order` (50 when unset), then name.
local function in_order()
  local names = vim.tbl_keys(registered)
  table.sort(names, function(a, b)
    local oa, ob = registered[a].order or 50, registered[b].order or 50
    if oa ~= ob then
      return oa < ob
    end
    return a < b
  end)
  return names
end

---@return {name: string, feature: table, opts: table, overridden: string[]}[]
function M.list()
  discover()
  local file = settings.load()
  local out = {}
  for _, name in ipairs(in_order()) do
    local opts, overridden = settings.resolve(name, registered[name], file)
    table.insert(out, { name = name, feature = registered[name], opts = opts, overridden = overridden })
  end
  return out
end

--- One feature's effective options.
---@param name string
function M.options(name)
  discover()
  return (settings.resolve(name, registered[name], settings.load()))
end

--- Starts every enabled feature. A feature that fails doesn't stop the rest.
---@return string[] errors one per problem, ready to show the user
function M.start_all()
  discover()
  local file = settings.load()
  local errors = vim.tbl_map(function(name)
    return ("Sailfish settings name a feature that doesn't exist: '%s'"):format(name)
  end, settings.unknown_features(file, registered))
  for _, name in ipairs(in_order()) do
    local ok, err = pcall(function()
      local opts = settings.resolve(name, registered[name], file)
      if opts.enabled then
        registered[name].start(opts)
      end
    end)
    if not ok then
      table.insert(errors, ("Sailfish feature '%s' failed to start: %s"):format(name, err))
    end
  end
  return errors
end

return M
