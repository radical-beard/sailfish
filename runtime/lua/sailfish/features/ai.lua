-- Claude in the editor:
--   :Sailfish ask [question]        a conversation about the code (side panel)
--   :'<,'>Sailfish edit [what]      rewrite the selection, review the diff, apply
--   :Sailfish explain               what's wrong on this line, and how to fix it
-- Cmd+L asks and Cmd+K edits the selection (Ctrl+Shift+L / K off macOS),
-- unless the config already uses those keys. Credentials come from
-- ANTHROPIC_API_KEY, ANTHROPIC_AUTH_TOKEN or an `ant auth login` profile.
local commands = require("sailfish.commands")
local keys = require("sailfish.keys")

local M = {
  description = "Claude in the editor: ask about code, rewrite selections, explain problems",
  order = 40,
  defaults = { enabled = true, model = "claude-opus-5-5", effort = "medium", max_tokens = 64000, base_url = "" },
  choices = { effort = { "low", "medium", "high", "xhigh", "max" } },
}

-- Asks `question` about the current buffer, or prompts for one when it's "".
---@param chat sailfish.Chat
local function ask(chat, question, range)
  local source = { buf = vim.api.nvim_get_current_buf(), range = range }
  if question ~= "" then
    return chat:ask(question, source)
  end
  vim.ui.input({ prompt = "Ask Claude: " }, function(input)
    if input and input ~= "" then
      chat:ask(input, source)
    end
  end)
end

-- Has Claude rewrite lines first..last as instructed, or prompts for the
-- instruction when it's "".
local function rewrite(settings, instruction, first, last)
  local buf = vim.api.nvim_get_current_buf()
  local function go(what)
    require("sailfish.ai.edit").run(settings, what, buf, first, last)
  end
  if instruction ~= "" then
    return go(instruction)
  end
  vim.ui.input({ prompt = "Edit lines " .. first .. "-" .. last .. ": " }, function(input)
    if input and input ~= "" then
      go(input)
    end
  end)
end

local function explain(chat)
  local line = vim.api.nvim_win_get_cursor(0)[1]
  local here = vim.diagnostic.get(0, { lnum = line - 1 })
  local question = #here > 0 and "Explain the problem reported on line " .. line .. " and how to fix it."
    or "Explain what line " .. line .. " does, and point out anything wrong with it."
  ask(chat, question, { first = line - 20, last = line + 20 })
end

-- The visual selection's line range, after leaving visual mode.
local function selection()
  vim.cmd("normal! \27")
  return vim.fn.line("'<"), vim.fn.line("'>")
end

local function bind_default_keys(chat, settings)
  local ask_key, edit_key = keys.chord("<D-l>", "<C-S-l>"), keys.chord("<D-k>", "<C-S-k>")
  keys.offer("n", ask_key, function()
    ask(chat, "")
  end, "Ask Claude")
  keys.offer("v", ask_key, function()
    local first, last = selection()
    ask(chat, "", { first = first, last = last })
  end, "Ask Claude about the selection")
  keys.offer("v", edit_key, function()
    local first, last = selection()
    rewrite(settings, "", first, last)
  end, "Edit the selection with Claude")
end

function M.start(opts)
  local chat = require("sailfish.ai.chat").new(opts)
  commands.register("ask", {
    desc = "ask Claude about the code (the selection, or the current file)",
    run = function(args, cmd)
      ask(chat, table.concat(args, " "), cmd.range > 0 and { first = cmd.line1, last = cmd.line2 } or nil)
    end,
  })
  commands.register("edit", {
    desc = "have Claude rewrite the selected lines, then review the diff",
    run = function(args, cmd)
      local first, last = cmd.line1, cmd.line2
      if cmd.range == 0 then
        first, last = vim.fn.line("."), vim.fn.line(".")
      end
      rewrite(opts, table.concat(args, " "), first, last)
    end,
  })
  commands.register("explain", {
    desc = "ask Claude what's wrong on this line and how to fix it",
    run = function()
      explain(chat)
    end,
  })
  local palette = require("sailfish.palette")
  palette.register({ title = "AI: Ask Claude about this file", run = function() ask(chat, "") end })
  palette.register({ title = "AI: Explain this line's problem", run = function() explain(chat) end })
  palette.register({ title = "AI: New conversation", run = function() chat:reset() end })
  bind_default_keys(chat, opts)
end

return M
