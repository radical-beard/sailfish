-- Records what the config does, so it can be debugged without guesswork:
-- modules loaded and files sourced (with timings), keymaps, autocmds and
-- commands defined (with the code that defined them) and run (with timings).
--
-- Installed before the config loads. Definitions are seen by wrapping the
-- Lua APIs that make them; Lua callbacks are wrapped so each run is timed.
-- Every definition keeps running statistics; the timeline only takes
-- discrete events (keymaps, commands, plugin loads, errors, anything slow),
-- so a CursorMoved autocmd can't bury what the user actually did.
-- Definitions are dropped when what they define goes away, so a long
-- session doesn't accumulate them.
local M = {}

local kCapacity = 3000
local kSlowMs = 8
local kMaxConflicts = 200
-- Autocmds created between checks for ones that no longer exist.
local kPruneEvery = 500
-- Autocmd events that fire continuously; they get statistics, not timeline entries.
local continuous = {
  CursorMoved = true, CursorMovedI = true, CursorHold = true, CursorHoldI = true, TextChangedI = true,
  TextChangedP = true, WinScrolled = true, ModeChanged = true, SafeState = true, BufEnter = true,
  WinEnter = true, BufLeave = true, WinLeave = true, InsertCharPre = true, CmdlineChanged = true,
  OptionSet = true, TextChanged = true, BufWinEnter = true, FocusGained = true, FocusLost = true,
}

local start = vim.uv.hrtime()
local timeline = {}
local head = 0
local depth = 0

---@class sailfish.Site
---@field file string
---@field line integer

---@type table<string, {kind: string, name: string, sites: sailfish.Site[], count: integer, total_ms: number, max_ms: number, last: number?}>
M.definitions = {}

-- Keymaps defined again for the same mode, keys and scope: the later one
-- silently replaced the earlier. The most recent ones only.
---@type {mode: string, lhs: string, replaced: table, by: table}[]
M.conflicts = {}

---@type table<integer, string[]> definition ids local to each buffer
local by_buffer = {}
local autocmds_since_prune = 0

local function now_ms()
  return (vim.uv.hrtime() - start) / 1e6
end

local function push(event)
  event.t = event.t or now_ms()
  head = head % kCapacity + 1
  timeline[head] = event
  return event
end

--- Timeline entries, newest first.
function M.recent()
  local out = {}
  for i = 0, kCapacity - 1 do
    local event = timeline[(head - i - 1) % kCapacity + 1]
    if not event then
      break
    end
    out[#out + 1] = event
  end
  return out
end

function M.uptime_ms()
  return now_ms()
end

local runtime_dirs = { vim.env.VIMRUNTIME, vim.g.sailfish.runtime }

local function is_ours(file)
  for _, dir in ipairs(runtime_dirs) do
    if dir and vim.startswith(file, dir) then
      return true
    end
  end
  return false
end

-- Where the current definition came from: Lua call sites outside Neovim's
-- and Sailfish's runtimes, innermost first (a config's own keymap helper
-- shows up first, the line that called it second).
local function call_sites(level)
  local sites = {}
  for l = level, level + 10 do
    local info = debug.getinfo(l, "Sl")
    if not info then
      break
    end
    if info.source:sub(1, 1) == "@" then
      local file = info.source:sub(2)
      if not is_ours(file) then
        sites[#sites + 1] = { file = file, line = info.currentline }
        if #sites == 3 then
          break
        end
      end
    end
  end
  return sites
end

local function new_definition(kind, name, sites)
  return { kind = kind, name = name, sites = sites, count = 0, total_ms = 0, max_ms = 0 }
end

-- The buffer a definition is local to, from its `buf`/`buffer` option
-- (true or 0: the current buffer); nil when it's global.
local function scope(opts)
  local buf = opts and (opts.buf or opts.buffer)
  if not buf then
    return nil
  end
  return (buf == true or buf == 0) and vim.api.nvim_get_current_buf() or buf
end

local function define(id, def, buffer)
  M.definitions[id] = def
  if buffer then
    by_buffer[buffer] = by_buffer[buffer] or {}
    table.insert(by_buffer[buffer], id)
  end
end

local function forget_buffer(buf)
  for _, id in ipairs(by_buffer[buf] or {}) do
    M.definitions[id] = nil
  end
  by_buffer[buf] = nil
end

local function finish(def, event, t0, ...)
  local ms = (vim.uv.hrtime() - t0) / 1e6
  def.count = def.count + 1
  def.total_ms = def.total_ms + ms
  def.max_ms = math.max(def.max_ms, ms)
  def.last = now_ms()
  if event then
    event.duration = ms
  elseif ms >= kSlowMs then
    push({ kind = def.kind, name = def.name, site = def.sites[1], duration = ms, slow = true })
  end
  return ...
end

-- Runs `fn` timed; errors propagate untouched so tracebacks stay the user's.
local function timed(def, on_timeline, fn)
  return function(...)
    local event = on_timeline and push({ kind = def.kind, name = def.name, site = def.sites[1] }) or nil
    return finish(def, event, vim.uv.hrtime(), fn(...))
  end
end

local function trace_requires()
  local original = require
  _G.require = function(name)
    if package.loaded[name] ~= nil then
      return original(name)
    end
    local event = push({ kind = "require", name = name, depth = depth })
    local t0 = vim.uv.hrtime()
    depth = depth + 1
    -- Protected so a failed require (a pcall-ed probe for an optional
    -- module, say) can't leave the nesting depth raised.
    local ok, module = pcall(original, name)
    depth = depth - 1
    event.duration = (vim.uv.hrtime() - t0) / 1e6
    if not ok then
      error(module, 0)
    end
    return module
  end
end

local function trace_sourcing(group)
  local pending = {}
  vim.api.nvim_create_autocmd("SourcePre", {
    group = group,
    callback = function(ev)
      pending[ev.file] = { event = push({ kind = "source", name = ev.file, depth = depth }), t0 = vim.uv.hrtime() }
    end,
  })
  vim.api.nvim_create_autocmd("SourcePost", {
    group = group,
    callback = function(ev)
      local p = pending[ev.file]
      if p then
        p.event.duration = (vim.uv.hrtime() - p.t0) / 1e6
        pending[ev.file] = nil
      end
    end,
  })
end

local function canonical(lhs)
  return vim.fn.keytrans(vim.keycode(lhs))
end

--- The definitions key of a keymap, as the inspector looks it up.
---@param buffer integer? the buffer of a buffer-local keymap
function M.keymap_id(mode, lhs, buffer)
  return "keymap " .. mode .. " " .. canonical(lhs) .. (buffer and (" buffer " .. buffer) or "")
end

-- A definition's call chain: definitions made by the same code share one.
local function chain_key(def)
  local parts = {}
  for _, site in ipairs(def.sites) do
    parts[#parts + 1] = site.file .. ":" .. site.line
  end
  return table.concat(parts, "|")
end

---@type table<string, true> recorded conflicts, by mode, keys and the two chains
local seen_conflicts = {}

-- Code that runs again (an LspAttach handler, once per attaching client)
-- redefines its own keymaps: no conflict. Two definitions that keep
-- replacing each other are one conflict.
local function record_conflict(mode, lhs, replaced, by)
  local a, b = chain_key(replaced), chain_key(by)
  if a == b then
    return
  end
  local first, second = a, b
  if b < a then
    first, second = b, a
  end
  local key = table.concat({ mode, canonical(lhs), first, second }, "\0")
  if seen_conflicts[key] then
    return
  end
  seen_conflicts[key] = true
  table.insert(M.conflicts, { mode = mode, lhs = canonical(lhs), replaced = replaced, by = by })
  if #M.conflicts > kMaxConflicts then
    table.remove(M.conflicts, 1)
  end
end

local function trace_keymaps()
  local set, del = vim.keymap.set, vim.keymap.del
  vim.keymap.set = function(mode, lhs, rhs, opts)
    local modes, buffer = type(mode) == "table" and mode or { mode }, scope(opts)
    local def = new_definition("keymap", table.concat(modes, ",") .. " " .. canonical(lhs), call_sites(3))
    for _, m in ipairs(modes) do
      local id = M.keymap_id(m, lhs, buffer)
      if M.definitions[id] then
        record_conflict(m, lhs, M.definitions[id], def)
      end
      define(id, def, buffer)
    end
    if type(rhs) == "function" then
      rhs = timed(def, true, rhs)
    end
    return set(mode, lhs, rhs, opts)
  end
  -- A keymap deleted and defined again (lazy.nvim's key stubs) replaced nothing.
  vim.keymap.del = function(mode, lhs, opts)
    local buffer = scope(opts)
    for _, m in ipairs(type(mode) == "table" and mode or { mode }) do
      M.definitions[M.keymap_id(m, lhs, buffer)] = nil
    end
    return del(mode, lhs, opts)
  end
end

--- Drops the definitions of autocmds that no longer exist: deleted, fired
--- once, or their group cleared.
function M.prune()
  autocmds_since_prune = 0
  local live = {}
  for _, autocmd in ipairs(vim.api.nvim_get_autocmds({})) do
    if autocmd.id then
      live["autocmd " .. autocmd.id] = true
    end
  end
  for id, def in pairs(M.definitions) do
    if def.kind == "autocmd" and not live[id] then
      M.definitions[id] = nil
    end
  end
end

local function trace_autocmds()
  local create = vim.api.nvim_create_autocmd
  vim.api.nvim_create_autocmd = function(event, opts)
    local events = type(event) == "table" and event or { event }
    local name = table.concat(events, ",") .. (opts.pattern and (" " .. vim.inspect(opts.pattern)) or "")
    local def = new_definition("autocmd", name, call_sites(3))
    def.desc = opts.desc
    if type(opts.callback) == "function" then
      local discrete = not vim.iter(events):any(function(e)
        return continuous[e]
      end)
      opts = vim.tbl_extend("force", opts, { callback = timed(def, discrete, opts.callback) })
    end
    local id = create(event, opts)
    define("autocmd " .. id, def, scope(opts))
    autocmds_since_prune = autocmds_since_prune + 1
    if autocmds_since_prune >= kPruneEvery then
      M.prune()
    end
    return id
  end
end

local function trace_commands()
  local function wrap(create, has_buffer)
    return function(...)
      local args = { ... }
      local at = has_buffer and 2 or 1
      local buffer = has_buffer and scope({ buf = args[1] }) or nil
      local def = new_definition("command", args[at], call_sites(3))
      define("command " .. args[at] .. (buffer and (" buffer " .. buffer) or ""), def, buffer)
      if type(args[at + 1]) == "function" then
        args[at + 1] = timed(def, true, args[at + 1])
      end
      return create(unpack(args, 1, at + 2))
    end
  end
  vim.api.nvim_create_user_command = wrap(vim.api.nvim_create_user_command, false)
  vim.api.nvim_buf_create_user_command = wrap(vim.api.nvim_buf_create_user_command, true)
end

-- lazy.nvim announces each plugin it loads.
local function trace_plugin_loads(group)
  vim.api.nvim_create_autocmd("User", {
    group = group,
    pattern = "LazyLoad",
    callback = function(ev)
      push({ kind = "plugin", name = tostring(ev.data) })
    end,
  })
end

--- Errors reach the host, not Lua; it forwards them here for the timeline.
---@param text string
function M.record_error(text)
  vim.validate("text", text, "string")
  push({ kind = "error", name = vim.split(text, "\n", { trimempty = true })[1] or text, detail = text })
end

---@param group integer the augroup Sailfish's own autocmds live in
function M.install(group)
  trace_requires()
  trace_sourcing(group)
  trace_plugin_loads(group)
  vim.api.nvim_create_autocmd("BufWipeout", {
    group = group,
    callback = function(ev)
      forget_buffer(ev.buf)
    end,
  })
  trace_keymaps()
  trace_autocmds()
  trace_commands()
end

return M
