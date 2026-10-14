-- A floating, keyboard-driven panel: a title, styled lines that may carry an
-- item, and buffer-local key actions with hints in the footer. Sailfish's own
-- screens (reports, history, traces, the plugin shop) are panels.
local M = {}

---@class sailfish.PanelLine
---@field [integer] {[1]: string, [2]: string?}  chunks of {text, highlight}
---@field item any  what the line represents, handed to key actions

---@class sailfish.PanelKey
---@field desc string
---@field fn fun(panel: sailfish.Panel, item: any)
---@field hidden? boolean  leave out of the footer hints

---@class sailfish.PanelSpec
---@field title string
---@field lines sailfish.PanelLine[]
---@field keys table<string, sailfish.PanelKey>
---@field width? integer   columns; defaults to fit the content
---@field height? integer  rows; defaults to fit the content
---@field wrap? boolean    long lines wrap instead of running off the edge

---@class sailfish.Panel
---@field buf integer
---@field win integer
---@field private spec sailfish.PanelSpec
local Panel = {}
Panel.__index = Panel

local ns = vim.api.nvim_create_namespace("sailfish.panel")

local function line_text(line)
  local parts = {}
  for _, chunk in ipairs(line) do
    parts[#parts + 1] = chunk[1]
  end
  return table.concat(parts)
end

local function footer(keys)
  local hints = {}
  for key, action in pairs(keys) do
    if not action.hidden then
      hints[#hints + 1] = { key = key, desc = action.desc }
    end
  end
  table.sort(hints, function(a, b)
    return a.desc < b.desc
  end)
  local chunks = { { " " } }
  for _, hint in ipairs(hints) do
    vim.list_extend(chunks, { { hint.key, "Special" }, { " " .. hint.desc .. "   ", "Comment" } })
  end
  return chunks
end

function Panel:render()
  local texts = vim.tbl_map(line_text, self.spec.lines)
  vim.bo[self.buf].modifiable = true
  vim.api.nvim_buf_set_lines(self.buf, 0, -1, false, texts)
  vim.bo[self.buf].modifiable = false
  vim.api.nvim_buf_clear_namespace(self.buf, ns, 0, -1)
  for row, line in ipairs(self.spec.lines) do
    local col = 0
    for _, chunk in ipairs(line) do
      if chunk[2] and #chunk[1] > 0 then
        vim.api.nvim_buf_set_extmark(self.buf, ns, row - 1, col, { end_col = col + #chunk[1], hl_group = chunk[2] })
      end
      col = col + #chunk[1]
    end
  end
end

---@return any item of the line under the cursor
function Panel:item()
  local row = vim.api.nvim_win_get_cursor(self.win)[1]
  local line = self.spec.lines[row]
  return line and line.item
end

---@param lines sailfish.PanelLine[]
function Panel:set_lines(lines)
  self.spec.lines = lines
  self:render()
end

---@param title string
function Panel:set_title(title)
  vim.api.nvim_win_set_config(self.win, { title = { { " " .. title .. " ", "FloatTitle" } }, title_pos = "center" })
end

function Panel:close()
  if vim.api.nvim_win_is_valid(self.win) then
    vim.api.nvim_win_close(self.win, true)
  end
end

---@param spec sailfish.PanelSpec
---@return sailfish.Panel
function M.open(spec)
  local self = setmetatable({ spec = spec }, Panel)
  self.buf = vim.api.nvim_create_buf(false, true)
  vim.bo[self.buf].bufhidden = "wipe"
  vim.bo[self.buf].filetype = "sailfish"

  local width = spec.width
  if not width then
    width = 40
    for _, line in ipairs(spec.lines) do
      width = math.max(width, vim.fn.strdisplaywidth(line_text(line)) + 2)
    end
  end
  width = math.min(width, vim.o.columns - 6)
  local rows = #spec.lines
  if spec.wrap then
    rows = 0
    for _, line in ipairs(spec.lines) do
      rows = rows + math.max(1, math.ceil(vim.fn.strdisplaywidth(line_text(line)) / width))
    end
  end
  local height = math.min(spec.height or rows, vim.o.lines - 6)

  self.win = vim.api.nvim_open_win(self.buf, true, {
    relative = "editor",
    width = width,
    height = math.max(height, 1),
    row = math.floor((vim.o.lines - height) / 2) - 1,
    col = math.floor((vim.o.columns - width) / 2),
    style = "minimal",
    border = "rounded",
    title = { { " " .. spec.title .. " ", "FloatTitle" } },
    title_pos = "center",
    footer = footer(spec.keys),
    footer_pos = "left",
  })
  vim.wo[self.win].cursorline = true
  vim.wo[self.win].wrap = spec.wrap == true
  self:render()

  local function bind(key, fn)
    vim.keymap.set("n", key, fn, { buffer = self.buf, nowait = true, silent = true })
  end
  for key, action in pairs(spec.keys) do
    bind(key, function()
      action.fn(self, self:item())
    end)
  end
  bind("q", function()
    self:close()
  end)
  bind("<Esc>", function()
    self:close()
  end)
  return self
end

return M
