-- What Claude is shown of the code the user is working on: the file's path
-- and language, an excerpt with line numbers (the selection, the whole file
-- when it's small, or the lines around the cursor), and its diagnostics.
local M = {}

local kWholeFileLines = 400
local kAroundCursor = 150

---@class sailfish.CodeContext
---@field path string
---@field filetype string
---@field first integer
---@field last integer
---@field total integer
---@field lines string[]
---@field buf integer

---@param buf integer
---@param range? {first: integer, last: integer}  1-based, inclusive
---@return sailfish.CodeContext
function M.gather(buf, range)
  local total = vim.api.nvim_buf_line_count(buf)
  local first, last
  if range then
    first, last = math.max(1, range.first), math.min(total, range.last)
  elseif total <= kWholeFileLines then
    first, last = 1, total
  else
    local cursor = vim.api.nvim_win_get_cursor(vim.fn.bufwinid(buf) ~= -1 and vim.fn.bufwinid(buf) or 0)[1]
    first, last = math.max(1, cursor - kAroundCursor), math.min(total, cursor + kAroundCursor)
  end
  local name = vim.api.nvim_buf_get_name(buf)
  return {
    path = name ~= "" and vim.fn.fnamemodify(name, ":~:.") or "[unsaved buffer]",
    filetype = vim.bo[buf].filetype,
    first = first,
    last = last,
    total = total,
    lines = vim.api.nvim_buf_get_lines(buf, first - 1, last, false),
    buf = buf,
  }
end

local severity_names = { "error", "warning", "info", "hint" }

---@param ctx sailfish.CodeContext
local function diagnostics(ctx)
  local out = {}
  for _, d in ipairs(vim.diagnostic.get(ctx.buf)) do
    local line = d.lnum + 1
    if line >= ctx.first and line <= ctx.last then
      out[#out + 1] = string.format("line %d (%s): %s", line, severity_names[d.severity] or "?", d.message)
    end
  end
  return out
end

--- The context as prompt text.
---@param ctx sailfish.CodeContext
function M.render(ctx)
  local numbered = {}
  for i, line in ipairs(ctx.lines) do
    numbered[i] = string.format("%5d  %s", ctx.first + i - 1, line)
  end
  local parts = {
    string.format('<file path="%s" language="%s" lines="%d-%d of %d">', ctx.path, ctx.filetype, ctx.first, ctx.last, ctx.total),
    table.concat(numbered, "\n"),
    "</file>",
  }
  local diags = diagnostics(ctx)
  if #diags > 0 then
    vim.list_extend(parts, { "<diagnostics>", table.concat(diags, "\n"), "</diagnostics>" })
  end
  return table.concat(parts, "\n")
end

return M
