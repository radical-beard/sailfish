-- A live fuzzy picker: a one-line prompt over a results list, filtered as
-- you type with Neovim's own fuzzy matcher (matched characters highlighted).
-- <CR> picks, <C-n>/<C-p> or <Down>/<Up> move, <Esc> or <C-c> closes.
local M = {}

---@class sailfish.PickerItem
---@field text string     what is matched and shown
---@field hint? string    shown dimmed after the text (a key, a category)
---@field value any       handed to on_pick

---@class sailfish.PickerSpec
---@field title string
---@field items sailfish.PickerItem[]
---@field on_pick fun(item: sailfish.PickerItem)
---@field prompt? string

local ns = vim.api.nvim_create_namespace("sailfish.picker")

---@param spec sailfish.PickerSpec
function M.open(spec)
  local origin = vim.api.nvim_get_current_win()
  local width = math.min(vim.o.columns - 10, 100)
  local max_height = math.max(1, math.floor(vim.o.lines * 0.5))
  local height = math.min(#spec.items, max_height)
  local col = math.floor((vim.o.columns - width) / 2)
  local row = math.floor(vim.o.lines * 0.15)

  local results_buf = vim.api.nvim_create_buf(false, true)
  local prompt_buf = vim.api.nvim_create_buf(false, true)
  vim.bo[prompt_buf].bufhidden = "wipe"
  vim.bo[results_buf].bufhidden = "wipe"
  -- A prompt buffer: completion plugins and autopairs leave these alone.
  local prompt = spec.prompt or "› "
  vim.bo[prompt_buf].buftype = "prompt"
  vim.fn.prompt_setprompt(prompt_buf, prompt)

  local results_win = vim.api.nvim_open_win(results_buf, false, {
    relative = "editor", row = row + 3, col = col, width = width, height = math.max(height, 1),
    style = "minimal", border = "rounded", focusable = false,
  })
  local prompt_win = vim.api.nvim_open_win(prompt_buf, true, {
    relative = "editor", row = row, col = col, width = width, height = 1,
    style = "minimal", border = "rounded", title = { { " " .. spec.title .. " ", "FloatTitle" } }, title_pos = "center",
  })
  vim.wo[results_win].cursorline = true

  local shown = spec.items
  local selected = 1

  local function render(positions)
    local lines = {}
    for i, item in ipairs(shown) do
      lines[i] = "  " .. item.text .. (item.hint and ("  " .. item.hint) or "")
    end
    vim.api.nvim_buf_set_lines(results_buf, 0, -1, false, lines)
    vim.api.nvim_buf_clear_namespace(results_buf, ns, 0, -1)
    for i, item in ipairs(shown) do
      if item.hint then
        vim.api.nvim_buf_set_extmark(results_buf, ns, i - 1, #item.text + 4, { end_col = #lines[i], hl_group = "Comment" })
      end
      -- Match positions count characters; extmarks take bytes.
      for _, pos in ipairs(positions and positions[i] or {}) do
        local first = vim.str_byteindex(item.text, "utf-32", pos)
        local last = vim.str_byteindex(item.text, "utf-32", pos + 1)
        vim.api.nvim_buf_set_extmark(results_buf, ns, i - 1, first + 2, { end_col = last + 2, hl_group = "Special" })
      end
    end
    selected = math.min(math.max(selected, 1), math.max(#shown, 1))
    vim.api.nvim_win_set_height(results_win, math.max(1, math.min(#shown, max_height)))
    if #shown > 0 then
      vim.api.nvim_win_set_cursor(results_win, { selected, 0 })
    end
  end

  local filtered_for = prompt
  local function filter()
    local line = vim.api.nvim_buf_get_lines(prompt_buf, 0, 1, false)[1] or ""
    filtered_for = line
    local query = line:sub(#prompt + 1)
    if query == "" then
      shown = spec.items
      selected = 1
      return render(nil)
    end
    local matched = vim.fn.matchfuzzypos(spec.items, query, { key = "text" })
    shown, selected = matched[1], 1
    render(matched[2])
  end

  local function close()
    for _, win in ipairs({ prompt_win, results_win }) do
      if vim.api.nvim_win_is_valid(win) then
        vim.api.nvim_win_close(win, true)
      end
    end
    vim.cmd("stopinsert")
    if vim.api.nvim_win_is_valid(origin) then
      vim.api.nvim_set_current_win(origin)
    end
  end

  local function move(delta)
    if #shown > 0 then
      selected = (selected - 1 + delta) % #shown + 1
      vim.api.nvim_win_set_cursor(results_win, { selected, 0 })
    end
  end

  local function bind(lhs, fn)
    vim.keymap.set({ "i", "n" }, lhs, fn, { buffer = prompt_buf, nowait = true })
  end
  bind("<CR>", function()
    -- TextChangedI doesn't fire while typeahead is pending, so a fast typist
    -- (or a paste) can reach <CR> before the list caught up: filter now.
    if (vim.api.nvim_buf_get_lines(prompt_buf, 0, 1, false)[1] or "") ~= filtered_for then
      filter()
    end
    local item = shown[selected]
    close()
    if item then
      -- After the picker is gone, so the action sees the user's window.
      vim.schedule(function()
        spec.on_pick(item)
      end)
    end
  end)
  bind("<Esc>", close)
  bind("<C-c>", close)
  for _, key in ipairs({ "<C-n>", "<Down>", "<Tab>" }) do
    bind(key, function()
      move(1)
    end)
  end
  for _, key in ipairs({ "<C-p>", "<Up>", "<S-Tab>" }) do
    bind(key, function()
      move(-1)
    end)
  end
  vim.api.nvim_create_autocmd({ "TextChangedI", "TextChanged" }, { buffer = prompt_buf, callback = filter })
  vim.api.nvim_create_autocmd("BufLeave", { buffer = prompt_buf, once = true, callback = close })

  render(nil)
  vim.cmd("startinsert")
end

return M
