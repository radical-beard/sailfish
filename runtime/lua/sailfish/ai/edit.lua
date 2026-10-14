-- :Sailfish edit — Claude rewrites a range of lines as instructed. Nothing
-- changes until the proposal has been reviewed: it streams into a review
-- tab beside the original, turns into a diff when complete, and <CR>
-- applies it (one undo step) while q throws it away. The lines are tracked
-- while Claude writes, so edits elsewhere in the file don't misplace the
-- proposal; if the lines themselves change, it isn't applied.
local claude = require("sailfish.ai.claude")
local context = require("sailfish.ai.context")
local scratch = require("sailfish.ui.scratch")

local M = {}

local kSystem = table.concat({
  "You rewrite code inside the user's editor. You receive a file excerpt for context, a selection, and an instruction.",
  "Reply with only the new text that replaces the selection: no explanation and no Markdown code fences.",
  "Keep the indentation and style of the surrounding code, and change only what the instruction asks for.",
}, " ")

local kContextLines = 40

local ns = vim.api.nvim_create_namespace("sailfish.edit")

-- Models sometimes wrap code in a fence despite the instruction.
local function without_fence(text)
  local inner = text:match("^%s*```[%w_+-]*\n(.-)\n?```%s*$")
  return inner or text
end

local function prompt(instruction, buf, first, last, original)
  return table.concat({
    context.render(context.gather(buf, { first = first - kContextLines, last = last + kContextLines })),
    string.format('<selection lines="%d-%d">', first, last),
    table.concat(original, "\n"),
    "</selection>",
    "<instruction>" .. instruction .. "</instruction>",
  }, "\n")
end

---@class sailfish.EditReview
---@field buf integer        the buffer being edited
---@field original string[]  its lines as Claude was given them
---@field mark integer       tracks those lines through other edits
---@field tab integer
---@field before integer     the review's buffers
---@field after integer
---@field proposal_win integer
---@field cancel fun()?      stops Claude while it's writing
---@field finished boolean?
local Review = {}
Review.__index = Review

-- The tracked lines' rows now (0-based, inclusive), or nil if they can't
-- be found: the buffer was unloaded, or reloaded from disk.
function Review:tracked()
  if not vim.api.nvim_buf_is_loaded(self.buf) then
    return nil
  end
  local mark = vim.api.nvim_buf_get_extmark_by_id(self.buf, ns, self.mark, { details = true })
  if #mark == 0 then
    return nil
  end
  return mark[1], mark[3].end_row
end

function Review:status(text)
  if vim.api.nvim_win_is_valid(self.proposal_win) then
    vim.wo[self.proposal_win].winbar = text
  end
end

-- Stops Claude, if it's still writing; nothing it sends arrives after this.
function Review:stop()
  if self.cancel then
    self.cancel()
    self.cancel = nil
  end
end

-- Ends the review, whichever way it ends.
function Review:finish()
  if self.finished then
    return
  end
  self.finished = true
  self:stop()
  if vim.api.nvim_buf_is_loaded(self.buf) then
    vim.api.nvim_buf_del_extmark(self.buf, ns, self.mark)
  end
  if vim.api.nvim_tabpage_is_valid(self.tab) then
    vim.cmd("tabclose " .. vim.api.nvim_tabpage_get_number(self.tab))
  end
end

function Review:apply()
  local first, last = self:tracked()
  if not first or not vim.deep_equal(vim.api.nvim_buf_get_lines(self.buf, first, last + 1, false), self.original) then
    vim.notify("Sailfish: the lines changed (or their buffer closed) while Claude was writing, so the proposal can't be applied (q discards it)", vim.log.levels.WARN)
    return
  end
  local proposal = vim.api.nvim_buf_get_lines(self.after, 0, -1, false)
  self:finish()
  vim.api.nvim_buf_set_lines(self.buf, first, last + 1, false, proposal)
  vim.api.nvim_echo({ { "Applied Claude's edit (u undoes it)", "MoreMsg" } }, false, {})
end

function Review:bind(lhs, fn, desc)
  for _, b in ipairs({ self.before, self.after }) do
    vim.keymap.set("n", lhs, fn, { buffer = b, nowait = true, desc = desc })
  end
end

function Review:open(filetype, first, last)
  self.before = scratch.create("sailfish://edit/original", { filetype = filetype, bufhidden = "wipe", lines = self.original })
  vim.cmd("tab sbuffer " .. self.before)
  self.tab = vim.api.nvim_get_current_tabpage()
  vim.wo.winbar = string.format("%%#Comment# Lines %d-%d now", first, last)
  self.after = scratch.create("sailfish://edit/proposal", { filetype = filetype, bufhidden = "wipe" })
  vim.cmd("rightbelow vertical sbuffer " .. self.after)
  self.proposal_win = vim.api.nvim_get_current_win()
  self:bind("q", function()
    self:finish()
  end, "Discard the proposal")
  -- However the review's buffers go away (:q, :tabclose), Claude stops at
  -- once; the rest of the tab is closed once the wipe is over.
  for _, b in ipairs({ self.before, self.after }) do
    vim.api.nvim_create_autocmd("BufWipeout", {
      buffer = b,
      once = true,
      callback = function()
        self:stop()
        vim.schedule(function()
          self:finish()
        end)
      end,
    })
  end
  self:status("%#MoreMsg# Claude is writing…%#Comment#   q cancels")
end

---@param result sailfish.ClaudeResult
function Review:propose(result)
  self.cancel = nil
  if result.stop_reason == "refusal" or result.stop_reason == "max_tokens" then
    self:finish()
    local why = result.stop_reason == "refusal" and "Claude declined this edit" or "Claude's edit was cut off at the length limit"
    vim.notify("Sailfish: " .. why .. "; nothing was changed.", vim.log.levels.WARN)
    return
  end
  scratch.set_lines(self.after, 0, -1, vim.split(without_fence(result.text), "\n", { plain = true }))
  -- The proposal can be touched up before it's applied.
  vim.bo[self.after].modifiable = true
  for _, win in ipairs(vim.api.nvim_tabpage_list_wins(self.tab)) do
    vim.api.nvim_win_call(win, function()
      vim.cmd("diffthis")
    end)
  end
  self:bind("<CR>", function()
    self:apply()
  end, "Apply the proposal")
  self:status("%#MoreMsg# Claude's proposal%#Comment#   <CR> applies it · q discards it")
end

function Review:fail(message)
  self.cancel = nil
  self:finish()
  vim.notify("Sailfish: Claude: " .. message, vim.log.levels.ERROR)
end

---@param settings sailfish.ClaudeSettings the ai feature's options
---@param instruction string
---@param buf integer
---@param first integer  1-based
---@param last integer   inclusive
function M.run(settings, instruction, buf, first, last)
  local original = vim.api.nvim_buf_get_lines(buf, first - 1, last, false)
  local review = setmetatable({
    buf = buf,
    original = original,
    mark = vim.api.nvim_buf_set_extmark(buf, ns, first - 1, 0, { end_row = last - 1, end_col = #original[#original] }),
  }, Review)
  review:open(vim.bo[buf].filetype, first, last)
  review.cancel = claude.stream(settings, {
    system = kSystem,
    messages = { { role = "user", content = prompt(instruction, buf, first, last, original) } },
  }, {
    on_text = function(text)
      scratch.append(review.after, text)
    end,
    on_done = function(result)
      review:propose(result)
    end,
    on_error = function(message)
      review:fail(message)
    end,
  })
end

return M
