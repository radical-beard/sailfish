-- The boot sequence and the runtime's composition root. Everything here runs
-- before the user's config, so it only sets defaults the config can override
-- and installs observation hooks; features that depend on the config start
-- after VimEnter.
local commands = require("sailfish.commands")
local host = require("sailfish.host")

local M = {}

-- Neovim's own 'guifont' default names fonts that rarely have icons; prefer
-- the font the host picked, unless the config chooses one itself.
local function apply_default_font()
  local preferred = vim.g.sailfish.default_font
  local info = vim.api.nvim_get_option_info2("guifont", {})
  if preferred and vim.o.guifont == info.default then
    vim.o.guifont = preferred
  end
end

-- Plugins like noice or Neovim's ui2 draw messages and the cmdline
-- themselves; the host then hides its native ones (but still sees errors).
local function observe_message_ui_plugins()
  local attach, detach = vim.ui_attach, vim.ui_detach
  local claimed = {}
  vim.ui_attach = function(ns, opts, callback)
    if opts.ext_messages or opts.ext_cmdline then
      claimed[ns] = true
      host.notify("ui.plugin_messages", true)
    end
    return attach(ns, opts, callback)
  end
  vim.ui_detach = function(ns)
    if claimed[ns] then
      claimed[ns] = nil
      host.notify("ui.plugin_messages", next(claimed) ~= nil)
    end
    return detach(ns)
  end
end

-- Neovim's :restart hands the editor to a new server, which only a UI able
-- to reattach can follow (otherwise that server is left running, alone).
-- Here a hot reload does the same job, unsaved edits included, so a typed
-- :restart becomes one.
local function redirect_restart()
  vim.cmd([[cnoreabbrev <expr> restart getcmdtype() ==# ':' && getcmdline() ==# 'restart' ? 'Sailfish reload' : 'restart']])
end

-- Config safety and Sailfish's own screens. Their modules load on first use;
-- features register their own subcommands when they start.
local function register_builtin_commands()
  commands.register("reload", {
    desc = "reload the config now",
    run = function()
      host.notify("config.action", "reload")
    end,
  })
  commands.register("history", {
    desc = "browse and restore versions of your config",
    run = function()
      require("sailfish.history").open()
    end,
  })
  commands.register("inspect", {
    desc = "what your config defines, where, and what ran recently",
    run = function(args)
      require("sailfish.inspect").open(args[1])
    end,
    complete = function()
      return require("sailfish.inspect").view_names()
    end,
  })
  commands.register("trace", {
    desc = "what ran recently (the inspector's activity view)",
    run = function()
      require("sailfish.inspect").open("activity")
    end,
  })
  commands.register("palette", {
    desc = "every action in one fuzzy list (Cmd/Ctrl+Shift+P)",
    run = function()
      require("sailfish.palette").open()
    end,
  })
  commands.register("shop", {
    desc = "browse curated plugins and turn them on or off",
    run = function()
      require("sailfish.shop").open()
    end,
  })
  commands.register("settings", {
    desc = "view and change Sailfish settings",
    run = function()
      require("sailfish.settings_ui").open()
    end,
  })
end

local close_handler

-- A feature (sessions' hot exit) can take over closing the window.
function M.on_window_close(handler)
  close_handler = handler
end

local function close_window()
  if close_handler then
    close_handler()
  else
    vim.cmd("confirm qall")
  end
end

-- Everything the host can call (host.dispatch).
local function register_host_handlers()
  host.handle("state.handoff", function(dir)
    require("sailfish.state").handoff(dir)
  end)
  host.handle("report.show", function(report)
    require("sailfish.report").show(report)
  end)
  host.handle("report.reloaded", function(info)
    require("sailfish.report").reloaded(info)
  end)
  host.handle("tracer.record_error", function(text)
    require("sailfish.tracer").record_error(text)
  end)
  host.handle("window.close_requested", function()
    -- The host's request is answered before anything prompts or quits.
    vim.schedule(close_window)
  end)
end

-- A failed step is recorded rather than raised, so the steps after it still
-- run and the host always hears that startup finished.
local function attempt(errors, what, fn)
  local ok, err = pcall(fn)
  if not ok then
    table.insert(errors, what .. ": " .. err)
  end
end

local function after_config()
  -- Provenance recording was only for the config's own load.
  if vim.o.verbose == 1 then
    vim.o.verbose = 0
  end
  local errors = {}
  -- A hot-reload candidate resumes the editor it replaces.
  if vim.g.sailfish.import then
    attempt(errors, "Sailfish couldn't restore the editor", function()
      require("sailfish.state").import(vim.g.sailfish.import)
    end)
  end
  attempt(errors, "Sailfish couldn't start its features", function()
    vim.list_extend(errors, require("sailfish.features").start_all())
  end)
  attempt(errors, "Sailfish couldn't bind its default keys", function()
    require("sailfish.gui").install_default_keys()
    require("sailfish.keys").offer("n", "ZR", "<Cmd>Sailfish reload<CR>", "Reload the config (Sailfish's :restart)")
  end)
  attempt(errors, "Sailfish couldn't watch the config", function()
    require("sailfish.config_watch").start()
  end)
  host.notify("startup.done", { config = vim.fn.stdpath("config"), server = vim.v.servername, errors = errors })
end

function M.boot()
  -- Sailfish's own autocmds live here, so a config's bare `autocmd!` (which
  -- clears the default group) can't remove them.
  local group = vim.api.nvim_create_augroup("sailfish.boot", { clear = true })
  -- Before anything else: the tracer must see the config's first require.
  require("sailfish.tracer").install(group)
  -- With 'verbose' on, Neovim records the exact file and line behind every
  -- option and mapping the config sets (see :Sailfish inspect).
  vim.o.verbose = 1
  apply_default_font()
  require("sailfish.clipboard").install()
  observe_message_ui_plugins()
  commands.install()
  register_builtin_commands()
  redirect_restart()
  register_host_handlers()
  vim.api.nvim_create_autocmd("VimEnter", {
    group = group,
    once = true,
    callback = function()
      -- Scheduled so every other VimEnter handler (the config's) runs first.
      vim.schedule(after_config)
    end,
  })
end

return M
