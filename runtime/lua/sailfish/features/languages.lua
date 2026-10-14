-- Syntax and language intelligence for every language, set up the first
-- time a file of that type is opened, and never before:
--   * a treesitter parser, installed with nvim-treesitter and started
--   * a language server, installed with mason if it isn't on PATH, and enabled
-- Parsers and highlighting are only managed when nvim-treesitter is
-- Sailfish's own copy (a config with its own nvim-treesitter decides for
-- itself). Servers are added only for filetypes the config left without one.
local plugins = require("sailfish.features.plugins")
local registry = require("sailfish.registry.languages")

local M = {
  description = "Highlighting and language servers for any language, installed on first use",
  order = 30,
  defaults = { enabled = true, install = "auto", treesitter = true, lsp = true },
  choices = { install = { "auto", "ask", "never" } },
}

-- Asked for once per session.
local requested_parsers, requested_servers = {}, {}
-- Languages whose highlighting failed to start, reported once.
local failed_highlighting = {}

local function say(text)
  vim.api.nvim_echo({ { "Sailfish: " .. text, "MoreMsg" } }, false, {})
end

-- Asks first when configured to; `go` runs if the answer is yes.
local function permitted(opts, what, go)
  if opts.install == "never" then
    return
  elseif opts.install == "ask" then
    vim.ui.select({ "Install", "Not now" }, { prompt = "Install " .. what .. "?" }, function(choice)
      if choice == "Install" then
        go()
      end
    end)
  else
    go()
  end
end

local function buffers_with_filetype(ft)
  return vim.tbl_filter(function(buf)
    return vim.api.nvim_buf_is_loaded(buf) and vim.bo[buf].filetype == ft
  end, vim.api.nvim_list_bufs())
end

-- A parser whose queries don't load (a version mismatch, say) is reported
-- once rather than for every buffer.
local function highlight(buf, lang)
  local ok, err = pcall(vim.treesitter.start, buf, lang)
  if not ok and not failed_highlighting[lang] then
    failed_highlighting[lang] = true
    vim.notify("Sailfish: treesitter highlighting for " .. lang .. " failed: " .. err, vim.log.levels.WARN)
  end
end

local function ensure_parser(opts, ft, buf)
  if plugins.status("treesitter") ~= "loaded" then
    return
  end
  local lang = vim.treesitter.language.get_lang(ft) or ft
  if vim.treesitter.language.add(lang) then
    if not vim.treesitter.highlighter.active[buf] then
      highlight(buf, lang)
    end
    return
  end
  if requested_parsers[lang] then
    return
  end
  local ts = require("nvim-treesitter")
  if not vim.tbl_contains(ts.get_available(), lang) then
    return
  end
  requested_parsers[lang] = true
  permitted(opts, "the " .. lang .. " treesitter parser", function()
    say("installing the " .. lang .. " parser…")
    ts.install(lang):await(function(err)
      vim.schedule(function()
        if err then
          vim.notify("Sailfish: installing the " .. lang .. " parser failed: " .. tostring(err), vim.log.levels.ERROR)
          return
        end
        -- The install may have created the parser directory itself; Neovim
        -- caches which 'runtimepath' entries exist, so make it look again.
        vim.o.runtimepath = vim.o.runtimepath
        for _, b in ipairs(buffers_with_filetype(ft)) do
          highlight(b, lang)
        end
      end)
    end)
  end)
end

local function mason_package(server)
  local ok, mappings = pcall(function()
    return require("mason-lspconfig").get_mappings().lspconfig_to_package
  end)
  return ok and mappings[server] or nil
end

local function pick_server(ft)
  if registry.servers[ft] then
    return registry.servers[ft]
  end
  local ok, by_filetype = pcall(require, "mason-lspconfig.filetype_mappings")
  for _, server in ipairs(ok and by_filetype[ft] or {}) do
    if not registry.never_default[server] and mason_package(server) then
      return server
    end
  end
end

local function server_executable(server)
  local config = vim.lsp.config[server]
  local cmd = config and config.cmd
  return type(cmd) == "table" and vim.fn.executable(cmd[1]) == 1
end

-- Mason knows no packages until its registry index has been downloaded
-- (the first time, on a fresh install); `go` runs once it's ready.
local registry_ready = false
local function with_mason_registry(go)
  local ok, mason_registry = pcall(require, "mason-registry")
  if not ok then
    return
  end
  if registry_ready then
    return go(mason_registry)
  end
  mason_registry.refresh(vim.schedule_wrap(function()
    registry_ready = true
    go(mason_registry)
  end))
end

---@param mason_registry table
local function install_server(opts, ft, mason_registry)
  local server = pick_server(ft)
  if not server or vim.lsp.is_enabled(server) or requested_servers[server] then
    return
  end
  requested_servers[server] = true
  if server_executable(server) then
    vim.lsp.enable(server)
    return
  end
  local name = mason_package(server)
  if not (name and mason_registry.has_package(name)) then
    return
  end
  permitted(opts, server .. " (the " .. ft .. " language server)", function()
    say("installing " .. name .. " for " .. ft .. "…")
    mason_registry.get_package(name):install({}, vim.schedule_wrap(function(success, result)
      if success then
        say(name .. " installed")
        vim.lsp.enable(server)
      else
        vim.notify("Sailfish: installing " .. name .. " failed: " .. tostring(result), vim.log.levels.ERROR)
      end
    end))
  end)
end

local function ensure_server(opts, ft, buf)
  -- The config already handles this language: a server is attached, or one
  -- is enabled for the filetype (it may still be starting).
  if #vim.lsp.get_clients({ bufnr = buf }) > 0 or #vim.lsp.get_configs({ enabled = true, filetype = ft }) > 0 then
    return
  end
  with_mason_registry(function(mason_registry)
    install_server(opts, ft, mason_registry)
  end)
end

local function revisit_open_buffers()
  for _, buf in ipairs(vim.api.nvim_list_bufs()) do
    if vim.api.nvim_buf_is_loaded(buf) and vim.bo[buf].filetype ~= "" then
      vim.api.nvim_exec_autocmds("FileType", { buffer = buf, group = "sailfish.languages", modeline = false })
    end
  end
end

function M.start(opts)
  local group = vim.api.nvim_create_augroup("sailfish.languages", { clear = true })
  vim.api.nvim_create_autocmd("FileType", {
    group = group,
    callback = function(ev)
      -- After the config's own FileType handlers have had their turn.
      vim.schedule(function()
        if not vim.api.nvim_buf_is_valid(ev.buf) or vim.bo[ev.buf].buftype ~= "" then
          return
        end
        if opts.treesitter then
          ensure_parser(opts, ev.match, ev.buf)
        end
        if opts.lsp then
          ensure_server(opts, ev.match, ev.buf)
        end
      end)
    end,
  })
  -- Buffers already open (a restored session) count, now and once Sailfish's
  -- own plugins (nvim-treesitter, mason) have finished installing.
  vim.api.nvim_create_autocmd("User", { group = group, pattern = "SailfishPluginsReady", callback = revisit_open_buffers })
  revisit_open_buffers()
end

return M
