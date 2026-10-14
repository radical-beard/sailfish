-- :Sailfish ask — a conversation with Claude about the code, in a side
-- panel. Answers stream in as Markdown; follow-ups continue the same
-- conversation (append-only: earlier turns are sent back exactly as they
-- were). In the panel: i asks a follow-up, <C-c> stops an answer, q closes.
--
-- An answer that's abandoned (stopped, the conversation reset, the panel
-- wiped) is cancelled, so no later callback can reach the panel or the
-- conversation; its question leaves the conversation with it.
local claude = require("sailfish.ai.claude")
local context = require("sailfish.ai.context")
local scratch = require("sailfish.ui.scratch")

local M = {}

local kSystem = table.concat({
  "You are a senior software engineer pairing with the user inside Sailfish, a Neovim-based IDE.",
  "The user shows you code from their editor with line numbers and diagnostics.",
  "Answer concisely and concretely, in Markdown. When you suggest changes, show only the code that changes and say where it goes.",
}, " ")

---@class sailfish.Chat
---@field private settings sailfish.ClaudeSettings
---@field private messages table[]  the conversation so far
---@field private buf integer?      the panel, while it exists
---@field private cancel fun()?     stops the answer being streamed
local Chat = {}
Chat.__index = Chat

---@param settings sailfish.ClaudeSettings the ai feature's options
---@return sailfish.Chat
function M.new(settings)
  return setmetatable({ settings = settings, messages = {} }, Chat)
end

function Chat:write(text)
  scratch.append(self.buf, text)
  local win = vim.fn.bufwinid(self.buf)
  if win ~= -1 then
    vim.api.nvim_win_set_cursor(win, { vim.api.nvim_buf_line_count(self.buf), 0 })
  end
end

-- Abandons the answer being streamed, and the question it answers.
function Chat:abandon()
  if self.cancel then
    self.cancel()
    self.cancel = nil
    table.remove(self.messages)
  end
end

function Chat:create_panel()
  self.buf = scratch.create("sailfish://claude", { filetype = "markdown", bufhidden = "hide" })
  local function bind(lhs, fn, desc)
    vim.keymap.set("n", lhs, fn, { buffer = self.buf, nowait = true, desc = desc })
  end
  bind("q", function()
    vim.api.nvim_win_close(0, false)
  end, "Close the Claude panel")
  bind("i", function()
    self:prompt()
  end, "Ask a follow-up")
  bind("<C-c>", function()
    if self.cancel then
      self:abandon()
      self:write("\n\n*(stopped)*\n\n")
    end
  end, "Stop the answer")
  vim.api.nvim_create_autocmd("BufUnload", {
    buffer = self.buf,
    once = true,
    callback = function()
      self:abandon()
      self.buf = nil
    end,
  })
end

function Chat:show_panel()
  if not self.buf then
    self:create_panel()
  end
  if vim.fn.bufwinid(self.buf) == -1 then
    local win = vim.api.nvim_open_win(self.buf, false, { split = "right", win = -1, width = math.floor(vim.o.columns * 0.4) })
    local wo = vim.wo[win]
    wo.wrap, wo.linebreak, wo.number, wo.relativenumber, wo.signcolumn = true, true, false, false, "no"
  end
end

--- Asks a question, with code context when `source` names a buffer.
---@param question string
---@param source? {buf: integer, range?: {first: integer, last: integer}}
function Chat:ask(question, source)
  if self.cancel then
    vim.notify("Sailfish: Claude is still answering (<C-c> in its panel stops it)", vim.log.levels.WARN)
    return
  end
  self:show_panel()
  local text = question
  if source then
    text = context.render(context.gather(source.buf, source.range)) .. "\n\n" .. question
  end
  table.insert(self.messages, { role = "user", content = text })
  self:write((vim.api.nvim_buf_line_count(self.buf) > 1 and "\n\n" or "") .. "## You\n\n" .. question .. "\n\n## Claude\n\n")

  self.cancel = claude.stream(self.settings, { system = kSystem, messages = self.messages }, {
    on_text = function(answer)
      self:write(answer)
    end,
    on_done = function(result)
      self.cancel = nil
      if result.stop_reason == "refusal" then
        -- Nothing was answered; the question leaves the conversation too.
        table.remove(self.messages)
        self:write("\n\n> Claude declined to answer this.\n")
        return
      end
      table.insert(self.messages, { role = "assistant", content = result.content })
      if result.stop_reason == "max_tokens" then
        self:write("\n\n> (The answer reached the length limit.)\n")
      end
    end,
    on_error = function(message)
      self.cancel = nil
      table.remove(self.messages)
      self:write("\n\n> **Error:** " .. message .. "\n")
    end,
  })
end

--- Prompts for a follow-up question in the same conversation.
function Chat:prompt()
  vim.ui.input({ prompt = "Ask Claude: " }, function(question)
    if question and question ~= "" then
      self:ask(question)
    end
  end)
end

--- Starts over.
function Chat:reset()
  self:abandon()
  self.messages = {}
  if self.buf then
    scratch.set_lines(self.buf, 0, -1, {})
  end
end

return M
