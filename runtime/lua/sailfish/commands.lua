-- :Sailfish {subcommand}. Features register their subcommands here.
local M = {}

---@class sailfish.Subcommand
---@field desc string
---@field run fun(args: string[], cmd: table)
---@field complete? fun(lead: string): string[]  candidates for its arguments

---@type table<string, sailfish.Subcommand>
local subcommands = {}

---@param name string
---@param subcommand sailfish.Subcommand
function M.register(name, subcommand)
  vim.validate("name", name, "string")
  vim.validate("subcommand", subcommand, "table")
  vim.validate(name .. ".desc", subcommand.desc, "string")
  vim.validate(name .. ".run", subcommand.run, "function")
  vim.validate(name .. ".complete", subcommand.complete, "function", true)
  subcommands[name] = subcommand
end

--- Every subcommand, by name.
---@return {name: string, desc: string}[]
function M.list()
  local list = {}
  for name, subcommand in pairs(subcommands) do
    table.insert(list, { name = name, desc = subcommand.desc })
  end
  table.sort(list, function(a, b)
    return a.name < b.name
  end)
  return list
end

--- Runs a subcommand as if typed with no arguments.
---@param name string
function M.run(name)
  vim.api.nvim_cmd({ cmd = "Sailfish", args = { name } }, {})
end

local function overview()
  local lines = { { { "" } } }
  for _, subcommand in ipairs(M.list()) do
    table.insert(lines, { { string.format("  %-12s", subcommand.name), "Function" }, { subcommand.desc, "Normal" }, item = subcommand.name })
  end
  require("sailfish.ui.panel").open({
    title = "Sailfish",
    lines = lines,
    keys = {
      ["<CR>"] = {
        desc = "run",
        fn = function(p, name)
          if name then
            p:close()
            M.run(name)
          end
        end,
      },
    },
  })
end

local function starting_with(lead, candidates)
  return vim.tbl_filter(function(candidate)
    return vim.startswith(candidate, lead)
  end, candidates)
end

local function complete(lead, line)
  local words = vim.split(line, "%s+")
  if #words <= 2 then
    return starting_with(lead, vim.tbl_map(function(subcommand)
      return subcommand.name
    end, M.list()))
  end
  local subcommand = subcommands[words[2]]
  return subcommand and subcommand.complete and starting_with(lead, subcommand.complete(lead)) or {}
end

function M.install()
  vim.api.nvim_create_user_command("Sailfish", function(cmd)
    local name = cmd.fargs[1]
    if not name then
      overview()
    elseif subcommands[name] then
      -- `cmd` carries the range (:'<,'>Sailfish edit ...) and the raw args.
      subcommands[name].run(vim.list_slice(cmd.fargs, 2), cmd)
    else
      vim.notify("Sailfish: no subcommand '" .. name .. "'", vim.log.levels.ERROR)
    end
  end, {
    nargs = "*",
    range = true,
    desc = "Sailfish commands",
    complete = complete,
  })
end

return M
