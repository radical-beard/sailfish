-- Incremental Server-Sent Events parser: feed it bytes as they arrive, get
-- back complete events. An event is lines of "field: value" ended by a
-- blank line; multi-line `data` joins with "\n".
local M = {}

---@class sailfish.SseParser
---@field private pending string
local Parser = {}
Parser.__index = Parser

function M.new()
  return setmetatable({ pending = "" }, Parser)
end

---@param chunk string
---@return {event: string?, data: string}[]
function Parser:feed(chunk)
  self.pending = (self.pending .. chunk):gsub("\r\n", "\n")
  local events = {}
  while true do
    local stop = self.pending:find("\n\n", 1, true)
    if not stop then
      break
    end
    local block = self.pending:sub(1, stop - 1)
    self.pending = self.pending:sub(stop + 2)
    local event, data = nil, {}
    for line in (block .. "\n"):gmatch("(.-)\n") do
      local field, value = line:match("^(%w+): ?(.*)$")
      if field == "event" then
        event = value
      elseif field == "data" then
        data[#data + 1] = value
      end
    end
    if #data > 0 then
      events[#events + 1] = { event = event, data = table.concat(data, "\n") }
    end
  end
  return events
end

--- Whatever arrived after the last complete event (an error body, say).
function Parser:rest()
  return self.pending
end

return M
