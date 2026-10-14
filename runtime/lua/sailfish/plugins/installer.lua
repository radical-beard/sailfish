-- Installs catalog plugins as Neovim packages under Sailfish's data dir
-- (site/pack/sailfish/opt/<id>) and loads them with :packadd. Independent of
-- any plugin manager the config uses: lazy.nvim resets 'packpath', so the
-- site dir is (re)added right before loading.
--
-- A plugin's dir only appears once its clone is complete (clones land in a
-- temp dir beside it), and records the catalog ref it has checked out, so a
-- catalog that pins another ref gets it on the next start.
local M = {}

local function site()
  return vim.g.sailfish.data_dir .. "/site"
end

---@param id string
function M.path(id)
  return site() .. "/pack/sailfish/opt/" .. id
end

local function ref_file(dir)
  return dir .. "/.git/sailfish-ref"
end

---@param id string
function M.installed(id)
  return vim.uv.fs_stat(M.path(id) .. "/.git") ~= nil
end

--- Installed, at the ref the catalog pins.
---@param entry {id: string, ref: string}
function M.current(entry)
  local file = ref_file(M.path(entry.id))
  return vim.uv.fs_stat(file) ~= nil and vim.fn.readfile(file)[1] == entry.ref
end

local function git(args, cwd, done)
  vim.system(vim.list_extend({ "git" }, args), { cwd = cwd, text = true }, vim.schedule_wrap(function(result)
    done(result.code == 0, vim.trim(result.stderr))
  end))
end

local function clone(entry, done)
  local dir = M.path(entry.id)
  local tmp = dir .. ".tmp-" .. vim.fn.getpid()
  vim.fn.delete(tmp, "rf")
  vim.fn.mkdir(vim.fs.dirname(dir), "p")
  local url = "https://github.com/" .. entry.repo .. ".git"
  git({ "clone", "--quiet", "--filter=blob:none", "--branch", entry.ref, url, tmp }, nil, function(ok, err)
    if ok then
      vim.fn.writefile({ entry.ref }, ref_file(tmp))
      local renamed, rename_err = vim.uv.fs_rename(tmp, dir)
      -- Another Sailfish may have finished the same install first.
      ok, err = renamed ~= nil or M.current(entry), rename_err
    end
    vim.fn.delete(tmp, "rf")
    done(ok, err)
  end)
end

local function update(entry, done)
  local dir = M.path(entry.id)
  git({ "fetch", "--quiet", "origin", entry.ref }, dir, function(fetched, fetch_err)
    if not fetched then
      return done(false, fetch_err)
    end
    git({ "checkout", "--quiet", "--detach", "FETCH_HEAD" }, dir, function(ok, err)
      if ok then
        vim.fn.writefile({ entry.ref }, ref_file(dir))
      end
      done(ok, err)
    end)
  end)
end

--- Brings a plugin to the catalog's ref: cloned when it's missing, fetched
--- and checked out when it has another. done(ok, err).
---@param entry {id: string, repo: string, ref: string}
---@param done fun(ok: boolean, err: string?)
function M.install(entry, done)
  if M.installed(entry.id) then
    update(entry, done)
  else
    clone(entry, done)
  end
end

---@param id string
function M.load(id)
  if not vim.tbl_contains(vim.opt.packpath:get(), site()) then
    vim.opt.packpath:append(site())
  end
  vim.cmd.packadd(id)
end

return M
