-- Opens a file at a line: where an error points, or where the config
-- defined something. The file may have changed since the line was recorded.
local M = {}

---@param site {file: string, line: integer}
function M.open(site)
  vim.cmd("edit " .. vim.fn.fnameescape(site.file))
  local line = math.min(math.max(site.line, 1), vim.api.nvim_buf_line_count(0))
  vim.api.nvim_win_set_cursor(0, { line, 0 })
  vim.cmd("normal! zz")
end

return M
