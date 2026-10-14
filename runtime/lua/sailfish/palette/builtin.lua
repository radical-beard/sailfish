-- Built-in command palette actions. `run` is an Ex command or a function.
local function toggle(scope, option)
  return function()
    vim[scope][option] = not vim[scope][option]
  end
end

return {
  { title = "File: Save", run = "write" },
  { title = "File: Save all", run = "wall" },
  { title = "File: New buffer", run = "enew" },
  { title = "File: Close buffer", run = "confirm bdelete" },
  { title = "File: Reload from disk", run = "edit" },
  { title = "View: Split vertically", run = "vsplit" },
  { title = "View: Split horizontally", run = "split" },
  { title = "View: Close window", run = "close" },
  { title = "View: Keep only this window", run = "only" },
  { title = "View: New tab", run = "tabnew" },
  { title = "View: Toggle line numbers", run = toggle("wo", "number") },
  { title = "View: Toggle relative line numbers", run = toggle("wo", "relativenumber") },
  { title = "View: Toggle line wrapping", run = toggle("wo", "wrap") },
  { title = "View: Toggle whitespace characters", run = toggle("wo", "list") },
  { title = "View: Toggle spell checking", run = toggle("wo", "spell") },
  { title = "View: Zoom in (bigger font)", run = function() require("sailfish.gui").zoom(1) end },
  { title = "View: Zoom out (smaller font)", run = function() require("sailfish.gui").zoom(-1) end },
  { title = "View: Reset zoom", run = function() require("sailfish.gui").zoom(0) end },
  { title = "Search: Clear highlights", run = "nohlsearch" },
  { title = "Code: Go to definition", run = function() vim.lsp.buf.definition() end },
  { title = "Code: Go to declaration", run = function() vim.lsp.buf.declaration() end },
  { title = "Code: Go to type definition", run = function() vim.lsp.buf.type_definition() end },
  { title = "Code: Find references", run = function() vim.lsp.buf.references() end },
  { title = "Code: Find implementations", run = function() vim.lsp.buf.implementation() end },
  { title = "Code: Rename symbol", run = function() vim.lsp.buf.rename() end },
  { title = "Code: Code actions", run = function() vim.lsp.buf.code_action() end },
  { title = "Code: Show documentation", run = function() vim.lsp.buf.hover() end },
  { title = "Code: Format document", run = function() vim.lsp.buf.format({ async = true }) end },
  { title = "Code: Document symbols", run = function() vim.lsp.buf.document_symbol() end },
  { title = "Code: Restart language servers", run = "lsp restart" },
  { title = "Problems: Next", run = function() vim.diagnostic.jump({ count = 1, float = true }) end },
  { title = "Problems: Previous", run = function() vim.diagnostic.jump({ count = -1, float = true }) end },
  { title = "Problems: Show this line's", run = function() vim.diagnostic.open_float() end },
  { title = "Problems: List all (quickfix)", run = function() vim.diagnostic.setqflist() end },
  { title = "Terminal: Open below", run = "belowright split | terminal" },
  { title = "Editor: Quit", run = "confirm qall" },
}
