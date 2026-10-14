-- Moves a running editor into a fresh Neovim: windows, tabs and cwd (a
-- session file), registers, marks and history (shada), and modified buffers
-- with their undo history. A hot reload hands the whole editor over
-- (handoff); the sessions feature saves projects (export). import() resumes
-- either one at startup, after the config has loaded.
local M = {}

-- No "blank": windows of scratch buffers (Sailfish's panels, plugin
-- sidebars) would come back as empty plain buffers. Terminals can't be resumed.
local kSessionOptions = "buffers,curdir,folds,help,tabpages,winsize"

---@type string? the :Sailfish screen to reopen once the next hot reload is done
local reopen

local function path(dir, name)
  return dir .. "/" .. name
end

local function escape(file)
  return vim.fn.fnameescape(file)
end

-- Runs fn, then restore, even when fn fails.
local function finally(fn, restore)
  local ok, err = pcall(fn)
  restore()
  if not ok then
    error(err, 0)
  end
end

local function modified(buf)
  return vim.api.nvim_buf_is_loaded(buf) and vim.bo[buf].modified
end

--- Whether a modified buffer exists that a session can't carry: one a
--- plugin saves ('buftype' acwrite), like oil's directory buffers.
function M.has_uncarried_edits()
  for _, buf in ipairs(vim.api.nvim_list_bufs()) do
    if modified(buf) and vim.bo[buf].buftype == "acwrite" then
      return true
    end
  end
  return false
end

--- Asks the instance that replaces this one (the next hot reload) to
--- reopen a screen.
---@param subcommand string a :Sailfish subcommand
function M.reopen_after_reload(subcommand)
  reopen = subcommand
end

-- Writes the text and undo history of every modified buffer of a file (or
-- of no file) beside the state. Names are unique to this save, so the
-- previous state.json keeps matching its files until the new one replaces it.
local function save_buffers(dir)
  local stamp = tostring(vim.uv.hrtime())
  local buffers = {}
  for _, buf in ipairs(vim.api.nvim_list_bufs()) do
    if modified(buf) and vim.bo[buf].buftype == "" then
      local base = path(dir, ("buffer-%s-%d"):format(stamp, #buffers + 1))
      local entry = { name = vim.api.nvim_buf_get_name(buf), text = base .. ".txt", undo = base .. ".undo", filetype = vim.bo[buf].filetype }
      vim.fn.writefile(vim.api.nvim_buf_get_lines(buf, 0, -1, false), entry.text, "b")
      vim.api.nvim_buf_call(buf, function()
        vim.cmd("silent wundo! " .. escape(entry.undo))
      end)
      table.insert(buffers, entry)
    end
  end
  return buffers
end

-- The layout, written without touching the user's own session state
-- (v:this_session, SessionWritePost).
local function write_session(file)
  local options, this_session = vim.o.sessionoptions, vim.v.this_session
  vim.o.sessionoptions = kSessionOptions
  finally(function()
    vim.cmd("noautocmd mksession! " .. escape(file))
  end, function()
    vim.o.sessionoptions = options
    vim.v.this_session = this_session
  end)
end

-- state.json decides what import reads, so it's replaced in one rename;
-- buffer files no state refers to any more are removed after.
local function commit(dir, state)
  local file = path(dir, "state.json")
  vim.fn.writefile({ vim.json.encode(state) }, file .. ".tmp")
  assert(vim.uv.fs_rename(file .. ".tmp", file))
  local current = {}
  for _, entry in ipairs(state.buffers) do
    current[entry.text], current[entry.undo] = true, true
  end
  for name in vim.fs.dir(dir) do
    if vim.startswith(name, "buffer-") and not current[path(dir, name)] then
      os.remove(path(dir, name))
    end
  end
end

---@param unsaved "keep"|"drop"
---@param screen string? to reopen after a hot reload
local function save(dir, unsaved, screen)
  vim.fn.mkdir(dir, "p")
  local buffers = unsaved == "keep" and save_buffers(dir) or {}
  write_session(path(dir, "session.vim"))
  commit(dir, { buffers = buffers, reopen = screen })
end

--- Saves a project's session.
---@param dir string
---@param opts {unsaved: "keep"|"drop"}  keep: unsaved edits travel with it; drop: only the layout
function M.export(dir, opts)
  save(dir, opts.unsaved)
end

--- Hands the editor to the instance replacing this one (hot reload): with
--- its unsaved edits, registers, marks and history, and a screen to reopen.
---@param dir string
function M.handoff(dir)
  vim.validate("dir", dir, "string")
  save(dir, "keep", reopen)
  vim.cmd("wshada! " .. escape(path(dir, "state.shada")))
  reopen = nil
end

-- state.json is checked before it decides which files are read.
local function read_state(dir)
  local file = path(dir, "state.json")
  local ok, state = pcall(vim.json.decode, table.concat(vim.fn.readfile(file), "\n"))
  if not (ok and type(state) == "table" and vim.islist(state.buffers)) then
    error(file .. " isn't a Sailfish editor state" .. (ok and "" or (": " .. state)), 0)
  end
  for i, entry in ipairs(state.buffers) do
    for _, field in ipairs({ "name", "text", "undo", "filetype" }) do
      vim.validate(("buffers[%d].%s"):format(i, field), entry[field], "string")
    end
  end
  vim.validate("reopen", state.reopen, "string", true)
  return state
end

-- Registers and undo history are extras: one that won't restore is lost
-- and reported, not fatal.
local function try(what, fn)
  local ok, err = pcall(fn)
  if not ok then
    vim.notify("Sailfish: couldn't restore " .. what .. ": " .. err, vim.log.levels.WARN)
  end
end

-- The session file sets v:this_session and fires SessionLoadPost; it isn't
-- the user's session, so neither reaches them. `silent!`: a session that
-- can't fully restore (a deleted file) must not count as an error in the
-- new config.
local function restore_layout(file)
  local this_session, eventignore = vim.v.this_session, vim.o.eventignore
  vim.opt.eventignore:append("SessionLoadPost")
  finally(function()
    vim.cmd("silent! source " .. escape(file))
  end, function()
    vim.o.eventignore = eventignore
    vim.v.this_session = this_session
  end)
end

local function restore_buffer(entry)
  local buf = entry.name ~= "" and vim.fn.bufadd(entry.name) or vim.api.nvim_create_buf(true, false)
  vim.fn.bufload(buf)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, vim.fn.readfile(entry.text, "b"))
  if entry.name == "" then
    vim.bo[buf].filetype = entry.filetype
  end
  -- Undo history only applies to identical text, which it now is. A buffer
  -- with nothing to undo was saved without an undo file.
  if vim.uv.fs_stat(entry.undo) then
    try("the undo history of " .. (entry.name ~= "" and entry.name or "an unnamed buffer"), function()
      vim.api.nvim_buf_call(buf, function()
        vim.cmd("silent rundo " .. escape(entry.undo))
      end)
    end)
  end
end

local function restore(dir, state)
  local shada = path(dir, "state.shada")
  if vim.uv.fs_stat(shada) then
    try("registers, marks and history", function()
      vim.cmd("silent rshada! " .. escape(shada))
    end)
  end
  restore_layout(path(dir, "session.vim"))
  for _, entry in ipairs(state.buffers) do
    restore_buffer(entry)
  end
end

--- Resumes a handed-off editor or a saved session.
---@param dir string
function M.import(dir)
  local state = read_state(dir)
  -- The instance being replaced still holds swap files for these buffers;
  -- answering the ATTENTION prompt would stall the hidden new instance.
  local shortmess = vim.o.shortmess
  vim.opt.shortmess:append("A")
  finally(function()
    restore(dir, state)
  end, function()
    vim.o.shortmess = shortmess
  end)
  if state.reopen then
    vim.schedule(function()
      require("sailfish.commands").run(state.reopen)
    end)
  end
end

return M
