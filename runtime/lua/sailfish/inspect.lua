-- :Sailfish inspect — the config, explained. Every view lists things the
-- config created, each with the place that created it; <CR> opens that
-- place. Views: what ran recently, keymaps, options, commands, autocmds,
-- the startup profile, the costliest callbacks, and the config's files.
local location = require("sailfish.ui.location")
local panel = require("sailfish.ui.panel")
local tracer = require("sailfish.tracer")

local M = {}

local config_root = require("sailfish.config").root()

local function short(file)
  if vim.startswith(file, config_root .. "/") then
    return "config/" .. file:sub(#config_root + 2)
  end
  return vim.fn.fnamemodify(file, ":~")
end

-- A config's own helper (a keymap wrapper, say) is the innermost call site
-- of many definitions; what the user wants is the line that called it.
-- Recognize helpers by that fan-in and step past them.
local kHelperFanIn = 3

local function site_key(site)
  return site.file .. ":" .. site.line
end

-- How many definitions pass through each call site. Helpers (including
-- helpers that call helpers) pass many; the lines that use them, one.
local function fan_in()
  local counts = {}
  local seen = {}
  for _, def in pairs(tracer.definitions) do
    if not seen[def] then
      seen[def] = true
      for _, site in ipairs(def.sites) do
        counts[site_key(site)] = (counts[site_key(site)] or 0) + 1
      end
    end
  end
  return counts
end

local counts_cache

---@param def table tracer definition
---@return sailfish.Site?
local function best_site(def)
  counts_cache = counts_cache or fan_in()
  for _, site in ipairs(def.sites) do
    if (counts_cache[site_key(site)] or 0) <= kHelperFanIn then
      return site
    end
  end
  return def.sites[#def.sites]
end

---@param site sailfish.Site?
local function where(site)
  return site and (short(site.file) .. ":" .. site.line) or ""
end

-- Neovim's own record: a script id and line (precise for anything defined
-- while 'verbose' was on, which Sailfish arranges during config load).
local function script_site(sid, line)
  if not sid or sid <= 0 then
    return nil
  end
  local info = vim.fn.getscriptinfo({ sid = sid })[1]
  return info and { file = info.name, line = line or 0 } or nil
end

local function ms(v)
  return v and string.format("%7.2fms", v) or "         "
end

local function ago(t)
  local s = (tracer.uptime_ms() - t) / 1000
  if s < 60 then
    return string.format("%4ds ago", s)
  end
  return string.format("%4dm ago", s / 60)
end

local kind_style = {
  keymap = "Function", command = "Statement", autocmd = "Type", plugin = "Special", error = "ErrorMsg",
  require = "Identifier", source = "Identifier", option = "Constant",
}

local function row(chunks, site)
  chunks.item = site
  return chunks
end

local views = {}

views[1] = {
  name = "Activity",
  lines = function()
    local out = {}
    for _, e in ipairs(tracer.recent()) do
      if e.kind ~= "require" and e.kind ~= "source" then
        table.insert(out, row({
          { "  " .. ago(e.t) .. "  ", "Comment" },
          { string.format("%-8s", e.kind), kind_style[e.kind] or "Normal" },
          { string.format(" %-40s", e.name), e.kind == "error" and "ErrorMsg" or "Normal" },
          { ms(e.duration), e.slow and "WarningMsg" or "Comment" },
          { "  " .. where(e.site), "Directory" },
        }, e.site))
      end
    end
    return out
  end,
}

views[2] = {
  name = "Keymaps",
  lines = function()
    local out = {}
    local buf = vim.api.nvim_get_current_buf()
    for _, mode in ipairs({ "n", "i", "v", "x", "s", "o", "t", "c" }) do
      local maps = vim.list_extend(vim.api.nvim_get_keymap(mode), vim.api.nvim_buf_get_keymap(buf, mode))
      for _, m in ipairs(maps) do
        local def = tracer.definitions[tracer.keymap_id(mode, m.lhs, m.buffer == 1 and buf or nil)]
        local site = def and best_site(def) or script_site(m.sid, m.lnum)
        local what = m.desc or m.rhs or (m.callback and "<Lua function>") or ""
        table.insert(out, row({
          { string.format("  %s  ", mode), "Comment" },
          { string.format("%-16s", vim.fn.keytrans(m.lhsraw)), "Function" },
          { string.format("%-46s", what:sub(1, 46)), "Normal" },
          { where(site), "Directory" },
        }, site))
      end
    end
    return out
  end,
}

views[3] = {
  name = "Options",
  lines = function()
    local out = {}
    local names = vim.tbl_keys(vim.api.nvim_get_all_options_info())
    table.sort(names)
    for _, name in ipairs(names) do
      local info = vim.api.nvim_get_option_info2(name, {})
      local value = vim.api.nvim_get_option_value(name, {})
      if info.was_set and not vim.deep_equal(value, info.default) then
        local site = script_site(info.last_set_sid, info.last_set_linenr)
        table.insert(out, row({
          { string.format("  %-18s", name), "Constant" },
          { string.format("%-30s", vim.inspect(value):sub(1, 30)), "Normal" },
          { site and where(site) or "(set at runtime)", "Directory" },
        }, site))
      end
    end
    return out
  end,
}

views[4] = {
  name = "Commands",
  lines = function()
    local out = {}
    local commands = vim.api.nvim_get_commands({ builtin = false })
    local names = vim.tbl_keys(commands)
    table.sort(names)
    for _, name in ipairs(names) do
      local def = tracer.definitions["command " .. name]
      local site = def and best_site(def) or script_site(commands[name].script_id)
      table.insert(out, row({
        { string.format("  :%-22s", name), "Statement" },
        { def and string.format("%4dx %s  ", def.count, ms(def.total_ms)) or string.rep(" ", 18), "Comment" },
        { where(site), "Directory" },
      }, site))
    end
    return out
  end,
}

views[5] = {
  name = "Autocmds",
  lines = function()
    local out = {}
    for _, def in pairs(tracer.definitions) do
      if def.kind == "autocmd" then
        table.insert(out, row({
          { string.format("  %-34s", def.name:sub(1, 34)), "Type" },
          { string.format("%6dx %s  ", def.count, ms(def.total_ms)), "Comment" },
          { string.format("%-28s", (def.desc or ""):sub(1, 28)), "Normal" },
          { where(best_site(def)), "Directory" },
        }, best_site(def)))
      end
    end
    table.sort(out, function(a, b)
      return a[1][1] < b[1][1]
    end)
    return out
  end,
}

views[6] = {
  name = "Startup",
  lines = function()
    local out = {}
    local events = tracer.recent()
    for i = #events, 1, -1 do
      local e = events[i]
      if e.kind == "require" or e.kind == "source" then
        local slow = (e.duration or 0) >= 5
        local file = e.kind == "source" and e.name or nil
        table.insert(out, row({
          { string.format("  %8.1fms ", e.t), "Comment" },
          { ms(e.duration) .. "  ", slow and "WarningMsg" or "Comment" },
          { string.rep("  ", e.depth) .. (e.kind == "source" and short(e.name) or e.name), kind_style[e.kind] },
        }, file and { file = file, line = 1 } or nil))
      end
    end
    return out
  end,
}

views[7] = {
  name = "Cost",
  lines = function()
    local defs = vim.tbl_filter(function(d)
      return d.count > 0
    end, vim.tbl_values(tracer.definitions))
    table.sort(defs, function(a, b)
      return a.total_ms > b.total_ms
    end)
    local out = {}
    for _, d in ipairs(defs) do
      table.insert(out, row({
        { "  " .. ms(d.total_ms) .. " total ", "WarningMsg" },
        { ms(d.max_ms) .. " max ", "Comment" },
        { string.format("%7dx  ", d.count), "Comment" },
        { string.format("%-8s", d.kind), kind_style[d.kind] or "Normal" },
        { string.format(" %-34s", d.name:sub(1, 34)), "Normal" },
        { where(best_site(d)), "Directory" },
      }, best_site(d)))
    end
    return out
  end,
}

views[8] = {
  name = "Conflicts",
  lines = function()
    local out = {}
    for _, c in ipairs(tracer.conflicts) do
      local by, replaced = best_site(c.by), best_site(c.replaced)
      table.insert(out, row({
        { string.format("  %s  %-14s", c.mode, c.lhs), "Function" },
        { "  " .. where(by), "Directory" },
        { "  replaced  ", "WarningMsg" },
        { where(replaced), "Directory" },
      }, by))
    end
    if #out == 0 then
      out[1] = { { "  No keymap was defined twice.", "Comment" } }
    end
    return out
  end,
}

views[9] = {
  name = "Files",
  lines = function()
    local out = {}
    for name, kind in vim.fs.dir(config_root, { depth = 8 }) do
      if kind == "file" and not name:match("^%.git/") then
        local file = config_root .. "/" .. name
        table.insert(out, row({ { "  config/" .. name, "Directory" } }, { file = file, line = 1 }))
      end
    end
    table.sort(out, function(a, b)
      return a[1][1] < b[1][1]
    end)
    return out
  end,
}

--- The views' names, as :Sailfish inspect takes them.
function M.view_names()
  return vim.tbl_map(function(view)
    return view.name:lower()
  end, views)
end

local function title(view)
  local tabs = {}
  for i, v in ipairs(views) do
    tabs[#tabs + 1] = (v == view and "[" .. i .. " " .. v.name .. "]") or (i .. " " .. v.name)
  end
  return table.concat(tabs, "  ")
end

---@param view_name? string
function M.open(view_name)
  tracer.prune()
  counts_cache = nil
  local current = views[1]
  for _, v in ipairs(views) do
    if v.name:lower() == (view_name or ""):lower() then
      current = v
    end
  end
  local filter = ""

  -- Views describe the window the user came from, not this panel.
  local origin = vim.api.nvim_get_current_win()
  local function lines()
    local all = vim.api.nvim_win_call(origin, current.lines)
    if filter ~= "" then
      all = vim.tbl_filter(function(line)
        local text = table.concat(vim.tbl_map(function(c)
          return c[1]
        end, line))
        return text:lower():find(filter:lower(), 1, true) ~= nil
      end, all)
    end
    table.insert(all, 1, { { "" } })
    return all
  end

  local p
  local function show(view)
    current = view
    p:set_title(title(view) .. (filter ~= "" and ("   /" .. filter) or ""))
    p:set_lines(lines())
  end

  local keys = {
    ["<CR>"] = {
      desc = "open definition",
      fn = function(self, site)
        if site then
          self:close()
          location.open(site)
        end
      end,
    },
    ["/"] = {
      desc = "filter",
      fn = function()
        vim.ui.input({ prompt = "Filter: ", default = filter }, function(input)
          filter = input or ""
          show(current)
        end)
      end,
    },
    ["<Tab>"] = {
      desc = "next view",
      fn = function()
        for i, v in ipairs(views) do
          if v == current then
            show(views[i % #views + 1])
            return
          end
        end
      end,
    },
  }
  for i, v in ipairs(views) do
    keys[tostring(i)] = {
      desc = v.name:lower(),
      hidden = true,
      fn = function()
        show(v)
      end,
    }
  end
  p = panel.open({
    title = title(current),
    lines = lines(),
    keys = keys,
    width = vim.o.columns - 8,
    height = vim.o.lines - 8,
  })
end

return M
