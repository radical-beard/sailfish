-- Keys Sailfish binds on the config's behalf (window shortcuts, AI keys,
-- the defaults of plugins turned on in the shop) are offers: each is bound
-- only in the modes where the config hasn't already mapped those keys
-- globally, so the config's own mappings always win.
local M = {}

local mac = vim.fn.has("mac") == 1

--- A shortcut on a chord only a window can send: Cmd on macOS, usually
--- Ctrl+Shift elsewhere.
---@param on_mac string     e.g. "<D-l>"
---@param elsewhere string  e.g. "<C-S-l>"
function M.chord(on_mac, elsewhere)
  return mac and on_mac or elsewhere
end

-- maparg() reports the current buffer's own mapping first, which would hide
-- whether a global one exists.
local function mapped_globally(mode, lhs)
  local map = vim.fn.maparg(lhs, mode, false, true)
  if map.buffer ~= 1 then
    return map.lhs ~= nil
  end
  local raw = vim.keycode(lhs)
  for _, global in ipairs(vim.api.nvim_get_keymap(mode)) do
    if global.lhsraw == raw or global.lhsrawalt == raw then
      return true
    end
  end
  return false
end

---@param modes string|string[]
---@param lhs string
---@param rhs string|function
---@param desc string
function M.offer(modes, lhs, rhs, desc)
  for _, mode in ipairs(type(modes) == "string" and { modes } or modes) do
    if not mapped_globally(mode, lhs) then
      vim.keymap.set(mode, lhs, rhs, { desc = desc })
    end
  end
end

return M
