-- Sensible editor options for a fresh config (no init.lua or init.vim), so
-- a first start already feels like an IDE. A config that exists is never
-- touched: its options are its own.
local M = {
  description = "Sensible editor options when you don't have a config yet",
  order = 5,
  defaults = { enabled = true },
}

local options = {
  number = true,
  relativenumber = false,
  signcolumn = "yes",
  cursorline = true,
  scrolloff = 8,
  ignorecase = true,
  smartcase = true,
  undofile = true,
  expandtab = true,
  shiftwidth = 4,
  tabstop = 4,
  smartindent = true,
  wrap = false,
  splitright = true,
  splitbelow = true,
  updatetime = 250,
  confirm = true,
  termguicolors = true,
}

function M.start()
  if not require("sailfish.config").fresh() then
    return
  end
  for name, value in pairs(options) do
    vim.o[name] = value
  end
end

return M
