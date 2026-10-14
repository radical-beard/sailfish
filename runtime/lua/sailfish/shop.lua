-- :Sailfish shop — browse the curated catalog and turn plugins on or off.
-- A change is written to the settings file, which hot-reloads the editor:
-- the plugin is installed (if needed) and set up with sane defaults, and the
-- shop reopens where it was.
local catalog = require("sailfish.registry.catalog")
local features = require("sailfish.features")
local panel = require("sailfish.ui.panel")
local plugins = require("sailfish.features.plugins")
local settings = require("sailfish.settings")
local state = require("sailfish.state")

local M = {}

-- Sections in this order; categories the catalog adds later follow them.
local kSectionOrder = { "Essentials", "Navigation", "Coding", "Languages", "Git", "Editing", "UI", "AI", "Themes", "Libraries" }

local function sections()
  local list, seen = {}, {}
  local function add(name)
    if not seen[name] then
      seen[name] = true
      table.insert(list, name)
    end
  end
  for _, name in ipairs(kSectionOrder) do
    add(name)
  end
  for _, entry in ipairs(catalog) do
    add(entry.category)
  end
  return list
end

local function status(entry, opts)
  local where = plugins.status(entry.id)
  if where == "provided" then
    return "◆ in your config", "DiagnosticInfo"
  elseif where == "installing" then
    return "… installing", "DiagnosticWarn"
  elseif where == "failed" then
    return "✗ failed", "DiagnosticError"
  elseif plugins.enabled(entry.id, opts) then
    return "● on", "DiagnosticOk"
  end
  return "○", "Comment"
end

local function matches(entry, filter)
  if filter == "" then
    return true
  end
  local haystack = table.concat({ entry.id, entry.name, entry.description, entry.category, table.concat(entry.tags or {}, " ") }, " "):lower()
  return haystack:find(filter:lower(), 1, true) ~= nil
end

local function build(filter)
  local opts = features.options("plugins")
  local lines = { { { "" } } }
  for _, section in ipairs(sections()) do
    local entries = vim.tbl_filter(function(e)
      local in_section = section == "Essentials" and e.essential or (not e.essential and e.category == section)
      return in_section and matches(e, filter)
    end, catalog)
    if #entries > 0 then
      table.insert(lines, { { "  " .. section, "Title" } })
      for _, entry in ipairs(entries) do
        local text, hl = status(entry, opts)
        table.insert(lines, {
          { "    " },
          { string.format("%-18s", text), hl },
          { string.format("%-20s", entry.name), "Function" },
          { entry.description, "Normal" },
          item = entry,
        })
      end
      table.insert(lines, { { "" } })
    end
  end
  return lines
end

local function toggle(entry)
  if plugins.status(entry.id) == "provided" then
    vim.notify("Sailfish: " .. entry.name .. " is already set up by your config", vim.log.levels.INFO)
    return
  end
  local opts = features.options("plugins")
  local turn_on = not plugins.enabled(entry.id, opts)
  state.reopen_after_reload("shop")
  if turn_on and entry.group then
    for _, other in ipairs(catalog) do
      if other.group == entry.group and other.id ~= entry.id and plugins.enabled(other.id, opts) then
        settings.set({ "plugins", other.id }, false)
      end
    end
  end
  settings.set({ "plugins", entry.id }, turn_on)
end

function M.open()
  local filter = ""
  local p
  p = panel.open({
    title = "Sailfish shop",
    lines = build(filter),
    width = math.min(vim.o.columns - 8, 130),
    height = vim.o.lines - 8,
    keys = {
      ["<CR>"] = {
        desc = "turn on / off",
        fn = function(_, entry)
          if entry then
            toggle(entry)
          end
        end,
      },
      o = {
        desc = "open project page",
        fn = function(_, entry)
          if entry then
            vim.ui.open("https://github.com/" .. entry.repo)
          end
        end,
      },
      ["/"] = {
        desc = "search",
        fn = function()
          vim.ui.input({ prompt = "Search plugins: ", default = filter }, function(input)
            filter = input or ""
            p:set_title("Sailfish shop" .. (filter ~= "" and ("   /" .. filter) or ""))
            p:set_lines(build(filter))
          end)
        end,
      },
    },
  })
end

return M
