-- Claude over raw HTTP: streaming Messages API requests through curl.
-- The whole request, credentials included, reaches curl on stdin, so no key
-- ever appears in a process listing. Sailfish never stores credentials; an
-- `ant auth` token is kept in memory only, until the API rejects it.
local sse = require("sailfish.ai.sse")

local M = {}

local kApiVersion = "2023-06-01"
-- Re-runs a declined request on the model Anthropic recommends for the
-- refusal's category, inside the same streamed response.
local kFallbackBeta = "server-side-fallback-2026-07-01"
local kOAuthBeta = "oauth-2025-04-20"

---@type string? the `ant auth` access token
local ant_token

---@alias sailfish.Credentials {header: string, oauth: boolean}

local function bearer(token)
  return { header = "authorization: Bearer " .. token, oauth = true }
end

-- Credentials in the order Anthropic's SDKs resolve them; `done` gets nil
-- when there are none. `ant` runs in the background: it may refresh the
-- token over the network.
---@param done fun(credentials: sailfish.Credentials?)
local function credentials(done)
  if (vim.env.ANTHROPIC_API_KEY or "") ~= "" then
    return done({ header = "x-api-key: " .. vim.env.ANTHROPIC_API_KEY, oauth = false })
  elseif (vim.env.ANTHROPIC_AUTH_TOKEN or "") ~= "" then
    return done(bearer(vim.env.ANTHROPIC_AUTH_TOKEN))
  elseif ant_token then
    return done(bearer(ant_token))
  elseif vim.fn.executable("ant") == 0 then
    return done(nil)
  end
  vim.system({ "ant", "auth", "print-credentials", "--access-token" }, { text = true }, vim.schedule_wrap(function(r)
    local token = vim.trim(r.stdout)
    ant_token = r.code == 0 and token ~= "" and token or nil
    done(ant_token and bearer(ant_token))
  end))
end

-- A value in curl's config-file syntax.
local function quoted(s)
  return '"' .. (s:gsub("\\", "\\\\"):gsub('"', '\\"')) .. '"'
end

local function curl_config(base_url, auth, body)
  local betas = auth.oauth and { kFallbackBeta, kOAuthBeta } or { kFallbackBeta }
  local base = base_url ~= "" and base_url or vim.env.ANTHROPIC_BASE_URL or "https://api.anthropic.com"
  return table.concat({
    "url = " .. quoted(base:gsub("/+$", "") .. "/v1/messages"),
    "header = " .. quoted("content-type: application/json"),
    "header = " .. quoted("anthropic-version: " .. kApiVersion),
    "header = " .. quoted("anthropic-beta: " .. table.concat(betas, ",")),
    "header = " .. quoted(auth.header),
    "data-binary = " .. quoted(body),
  }, "\n")
end

--- The assistant turn to send back on a follow-up: blocks exactly as
--- streamed (thinking blocks must be echoed unchanged), minus what a
--- mid-output fallback invalidated — reasoning before the last fallback
--- marker — and the markers themselves.
---@param blocks table[]
function M.history_content(blocks)
  local last_fallback = 0
  for i, block in ipairs(blocks) do
    if block.type == "fallback" then
      last_fallback = i
    end
  end
  local out = {}
  for i, block in ipairs(blocks) do
    if block.type == "text" and block.text ~= "" then
      out[#out + 1] = { type = "text", text = block.text }
    elseif (block.type == "thinking" or block.type == "redacted_thinking") and i > last_fallback then
      out[#out + 1] = block
    end
  end
  return out
end

---@class sailfish.ClaudeSettings  the ai feature's options
---@field model string
---@field effort string
---@field max_tokens integer
---@field base_url string  "" for the default (or ANTHROPIC_BASE_URL)

---@class sailfish.ClaudeResult
---@field stop_reason string?   "end_turn", "max_tokens", "refusal", ...
---@field model string          the model that answered (a fallback may have)
---@field text string           the answer's text
---@field content table[]       the assistant turn to echo back on a follow-up

---@class sailfish.ClaudeHandlers
---@field on_text fun(text: string)
---@field on_done fun(result: sailfish.ClaudeResult)
---@field on_error fun(message: string)

-- Folds the stream's events into the answer: its content blocks as
-- streamed, the model, the stop reason, or the error the stream reported.
local function new_answer()
  local answer = { blocks = {} }
  function answer.add(data, handlers)
    if data.type == "message_start" then
      answer.model = data.message.model
    elseif data.type == "content_block_start" then
      answer.blocks[data.index + 1] = vim.deepcopy(data.content_block)
    elseif data.type == "content_block_delta" then
      local block, delta = answer.blocks[data.index + 1], data.delta
      if delta.type == "text_delta" then
        block.text = block.text .. delta.text
        handlers.on_text(delta.text)
      elseif delta.type == "thinking_delta" then
        block.thinking = block.thinking .. delta.thinking
      elseif delta.type == "signature_delta" then
        -- A thinking block may start without a signature field.
        block.signature = (block.signature or "") .. delta.signature
      end
    elseif data.type == "message_delta" then
      answer.stop_reason = data.delta.stop_reason
    elseif data.type == "error" then
      answer.error = data.error.message
    end
  end
  return answer
end

---@return sailfish.ClaudeResult
local function result(answer)
  local texts = {}
  for _, block in ipairs(answer.blocks) do
    if block.type == "text" then
      texts[#texts + 1] = block.text
    end
  end
  return { stop_reason = answer.stop_reason, model = answer.model, text = table.concat(texts), content = M.history_content(answer.blocks) }
end

-- What went wrong when curl failed. An HTTP error answers with a JSON error
-- body rather than events; a rejected token is forgotten.
local function failure(exit, rest)
  local ok, body = pcall(vim.json.decode, rest)
  local err = ok and type(body) == "table" and body.error
  if err and err.type == "authentication_error" then
    ant_token = nil
  end
  if err then
    return err.message
  end
  local stderr = vim.trim(exit.stderr)
  return stderr ~= "" and stderr or ("curl exited with code " .. exit.code)
end

---@param stream {cancelled: boolean}
local function spawn(settings, auth, body, stream, handlers)
  local parser = sse.new()
  local answer = new_answer()
  local function on_event(event)
    local ok, data = pcall(vim.json.decode, event.data)
    if ok and type(data) == "table" then
      answer.add(data, handlers)
    else
      answer.error = "Claude's answer arrived garbled: " .. event.data:sub(1, 200)
    end
  end
  return vim.system({ "curl", "--silent", "--show-error", "--no-buffer", "--fail-with-body", "--config", "-" }, {
    stdin = curl_config(settings.base_url, auth, body),
    stdout = function(_, chunk)
      if chunk then
        vim.schedule(function()
          for _, event in ipairs(parser:feed(chunk)) do
            if stream.cancelled then
              return
            end
            on_event(event)
          end
        end)
      end
    end,
  }, vim.schedule_wrap(function(exit)
    if stream.cancelled then
      return
    elseif exit.code ~= 0 then
      handlers.on_error(failure(exit, parser:rest()))
    elseif answer.error then
      handlers.on_error(answer.error)
    else
      handlers.on_done(result(answer))
    end
  end))
end

--- Streams one request; returns a function that cancels it. Once cancelled,
--- no handler runs.
---@param settings sailfish.ClaudeSettings
---@param request {system: string, messages: table[]}
---@param handlers sailfish.ClaudeHandlers
function M.stream(settings, request, handlers)
  -- Encoded now: the conversation may move on while credentials resolve.
  local body = vim.json.encode({
    model = settings.model,
    max_tokens = settings.max_tokens,
    stream = true,
    fallbacks = "default",
    thinking = { type = "adaptive" },
    output_config = { effort = settings.effort },
    cache_control = { type = "ephemeral" },
    system = request.system,
    messages = request.messages,
  })
  local stream = { cancelled = false }
  credentials(function(auth)
    if stream.cancelled then
      return
    elseif not auth then
      handlers.on_error("No Claude credentials found. Set ANTHROPIC_API_KEY, or run `ant auth login`.")
    else
      stream.job = spawn(settings, auth, body, stream, handlers)
    end
  end)
  return function()
    stream.cancelled = true
    -- The process may have finished already.
    if stream.job and not stream.job:is_closing() then
      stream.job:kill(15)
    end
  end
end

return M
