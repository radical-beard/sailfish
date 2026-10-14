# Sailfish

A keyboard-only desktop IDE that **is** Neovim. Sailfish embeds an unmodified
`nvim --embed` and draws it natively, so your existing `~/.config/nvim` — Lua,
Vimscript, lazy.nvim, every plugin — works as-is. Around it, Sailfish adds
what a terminal can't give Neovim and what a modern IDE does out of the box:
safe hot reload of your config, config history, a config inspector, sessions
with hot exit, autosave, a curated plugin shop, languages installed on first
use, a command palette, and Claude.

Written in C++20 (SDL3, FreeType, HarfBuzz) with a Lua runtime that loads
into Neovim alongside your config. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for the design and [docs/ROADMAP.md](docs/ROADMAP.md) for what could come next.

**Status:** 0.1, developed and tested on macOS (Apple Silicon) with Neovim
0.12.5. The Linux and Windows code paths exist but have never been compiled.

## Build and run

Requirements: CMake ≥ 3.24, a C++20 compiler, Ninja (recommended),
Neovim ≥ 0.12 on `PATH`, and `git` and `curl` (config history, plugin
installs, Claude). SDL3, FreeType and HarfBuzz are used when installed (e.g.
`brew install sdl3 freetype harfbuzz`) and otherwise built from pinned sources.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
./build/sailfish            # restores this directory's last session
```

```bash
./build/sailfish src/main.cpp    # any nvim arguments work
```

Run the unit tests with `./build/sailfish_tests`.

## Your config, unchanged

Sailfish runs your config exactly as terminal Neovim does and shares its
plugins, shada and undo files. Anything Sailfish-specific is optional and
should be guarded so the same config still works in a terminal:

```lua
if vim.g.sailfish then
  require("sailfish").setup({
    autosave = { enabled = true, mode = "on_focus_change" },
    plugins = { harpoon = true, lualine = false },
    ai = { effort = "high" },
  })
end
```

Sailfish only adds keymaps on keys your config leaves unused, and the
plugin shop never installs a plugin your config already provides.

## What it adds

| | |
|---|---|
| **Hot reload with a safety net** | Save any file in your config and the editor reloads into it — windows, unsaved edits and undo history carried over. The new config starts in a second Neovim first; if it reports errors (or hangs) you keep working in the old one and get a report with each error's file and line. A reload waits until you're back in Normal mode, so it never lands mid-insert or mid-prompt. |
| **Config history** | Every config change is recorded in a shadow git history (your own repo is never touched) and marked good or bad by how it started. If the config fails at startup, Sailfish offers to load the last working version. `:Sailfish history` shows diffs and restores any version. |
| **Inspector** | `:Sailfish inspect` answers "where is this set?": every keymap, option, command and autocmd your config defines, with the exact file and line (`<CR>` jumps there), keymap conflicts (one definition silently replacing another), startup time per module, and recent activity (`:Sailfish trace`). |
| **Settings** | `:Sailfish settings` lists every Sailfish option and changes it in place; the values live in `<config>/sailfish.lua`, plain Lua you can also edit. |
| **Plugin shop** | `:Sailfish shop` — 41 curated plugins with sane defaults, pinned versions and dependencies. `<CR>` turns one on or off (installed and set up automatically). A fresh config gets the essentials: Telescope, treesitter, LSP + Mason, blink.cmp, gitsigns, lualine, oil, which-key, autopairs, surround, conform. |
| **Languages on first use** | Open a file of a new type and Sailfish installs its treesitter parser and language server (via Mason when it isn't on `PATH`) — never before. Filetypes your config already serves are left alone. `install = "ask"` prompts first. |
| **Sessions and hot exit** | Each working directory reopens where you left it. Closing the window with unsaved changes keeps them — still unsaved, with undo — for next time. |
| **Autosave** | Off by default; VS Code's modes: `after_delay`, `on_focus_change`, `on_window_change`. Never overwrites a file changed on disk. |
| **Command palette** | Cmd+Shift+P (Ctrl+Shift+P elsewhere): Sailfish commands, editor and LSP actions, every described keymap and user command, in one fuzzy list. |
| **Claude** | `:Sailfish ask` (Cmd+L) opens a streaming conversation about the current file or selection; `:'<,'>Sailfish edit` (Cmd+K on a selection) rewrites code and shows a diff you apply with `<CR>` (one undo step) or discard with `q`; `:Sailfish explain` explains the diagnostic on the cursor line. |
| **A real GUI** | Native cmdline, messages and completion menu (no "Press ENTER"), toasts for background messages, smooth scrolling, floating-window shadows, font fallback with bundled Nerd Font symbols, font zoom (Cmd+= / Cmd+- / Cmd+0), and chords a terminal can't send (`<D-…>`, `<C-S-…>`). Window size and position are remembered. Color emoji need FreeType from the system (`brew install freetype`). |
| **Keyboard only** | No mouse input at all; the pointer is hidden over the window. |

## Commands

`:Sailfish` alone lists these; `:Sailfish <Tab>` completes them.

| Command | |
|---|---|
| `reload` | reload the config now (a typed `:restart` and `ZR` do the same) |
| `history` | browse and restore versions of your config |
| `inspect [view]` | keymaps, options, commands, autocmds, conflicts, startup cost, activity |
| `trace` | what ran recently |
| `settings` | view and change Sailfish settings |
| `shop` | browse curated plugins and turn them on or off |
| `palette` | the command palette |
| `ask [question]` | ask Claude about the selection or file |
| `edit [instruction]` | have Claude rewrite the selected lines, then review the diff |
| `explain` | ask Claude what's wrong on this line and how to fix it |

In Sailfish's panels, the keys are shown at the bottom; `q` closes.

## Claude setup

The AI features call the Claude API (`claude-opus-5-5`, adaptive thinking,
streaming) with credentials from, in order: `ANTHROPIC_API_KEY`,
`ANTHROPIC_AUTH_TOKEN`, or an `ant auth login` profile. The key is passed to
`curl` on stdin, never on the command line. Model, effort (`low` … `max`) and
token limit are settings under `ai`. Requests opt into server-side fallback
(`fallbacks: "default"`): a request the model's safety classifiers decline is
re-run server-side on Anthropic's recommended fallback model instead of
failing. `ai = { enabled = false }` turns the feature off entirely.

## Extending Sailfish

Everything registers; nothing requires editing Sailfish's core.

```lua
local sailfish = require("sailfish")
-- A feature: shows on the settings screen, starts after the config loads.
sailfish.register_feature("wordcount", {
  description = "Word count in the statusline",
  defaults = { enabled = true },
  start = function(opts) --[[ ... ]] end,
})
-- A :Sailfish subcommand.
sailfish.register_command("hello", { desc = "say hello", run = function(args) print("hello") end })
-- A palette action.
require("sailfish.palette").register({ title = "Git: Status", run = "Git" })
```

Curated plugins are entries in `runtime/lua/sailfish/registry/catalog.lua`;
filetype → language server mappings live in `registry/languages.lua`.

## Where data lives

Everything Sailfish stores is under `~/.local/share/sailfish/` on every OS:
`config-history/` (shadow git repos), `snapshots/` (last working configs),
`sessions/`, `site/` (shop plugins, as Neovim packages), `state/` (window
geometry), `run/`, `logs/`. Your Neovim config stays in `stdpath("config")`;
Sailfish adds only `sailfish.lua` there, and only when you change a setting.
Language servers and parsers go where Mason and nvim-treesitter always put
them, under `stdpath("data")`, so terminal Neovim can use them too.

## Developing

`tests/mock_claude.py` stands in for the Claude API so the AI features can be
exercised without network access or cost:

```bash
python3 tests/mock_claude.py 47321 /tmp/last_request.json
```

```bash
ANTHROPIC_BASE_URL=http://127.0.0.1:47321 ANTHROPIC_API_KEY=mock ./build/sailfish
```

The host publishes the active Neovim's server address to
`~/.local/share/sailfish/run/current-server`, so a running editor can be
driven from scripts with `nvim --server <address> --remote-expr …`.

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT license ([LICENSE-MIT](LICENSE-MIT))

at your option.

Unless you explicitly state otherwise, any contribution intentionally
submitted for inclusion in Sailfish by you, as defined in the Apache-2.0
license, shall be dual licensed as above, without any additional terms or
conditions.
