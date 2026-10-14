-- The RPC bridge to the Sailfish host process.
--
-- Runtime → host: `host.request("clipboard.get")` / `host.notify("startup.done", {})`
-- name methods registered by host services ("sailfish." is prepended).
-- Host → runtime: the host's only entry point is
-- `require("sailfish.host").dispatch(name, ...)`, which runs the handler the
-- runtime registered under that name with `host.handle` (see core.lua).
local M = {}

local function channel()
  return vim.g.sailfish.channel
end

---@param method string
function M.notify(method, ...)
  vim.rpcnotify(channel(), "sailfish." .. method, ...)
end

---@param method string
function M.request(method, ...)
  return vim.rpcrequest(channel(), "sailfish." .. method, ...)
end

---@type table<string, fun(...): any>
local handlers = {}

---@param name string
---@param fn fun(...): any
function M.handle(name, fn)
  handlers[name] = fn
end

---@param name string
function M.dispatch(name, ...)
  local fn = handlers[name]
  if not fn then
    error("sailfish: no runtime handler '" .. name .. "'")
  end
  return fn(...)
end

return M
