# Sailfish architecture

Sailfish is a keyboard-only desktop IDE that is Neovim: the same Lua/Vimscript
configuration, the same plugins, the same API. It adds what a terminal can't
give Neovim, plus what a modern IDE is expected to do out of the box.

**The problem in one sentence:** a native C++ host that embeds an unmodified
Neovim over its UI protocol, and layers IDE services (config safety, sessions,
curated plugins, on-demand languages, AI) around it without breaking existing
configurations.

## The core decision: embed real Neovim

"Support all the same plugin APIs" means `vim.api` (hundreds of functions),
`vim.fn` (every Vimscript function), Vimscript itself, `vim.uv`, LSP,
treesitter, remote plugins, and every quirk plugins depend on. Reimplementing
that is a multi-year effort that would still be subtly incompatible. Instead,
Sailfish runs a real `nvim --embed` child process and draws it, the way
Neovide and VimR do. Compatibility is total because it *is* Neovim.

The process boundary is an asset, not a cost:

| Capability | Why the boundary enables it |
|---|---|
| Safe hot reload | Start a *second* Neovim with the new config; swap to it only if it starts cleanly. The old instance *is* the "previous working build". |
| Crash isolation | A Neovim crash can't corrupt the host; sessions are saved periodically, and the host can relaunch into the last one (see the roadmap). |
| Remote editing | The same protocol runs over TCP/SSH: a local GUI on a remote Neovim. |
| GUI-only keys | The host sends `<D-s>`, `<C-S-a>`, `<C-i>` distinct from `<Tab>` — keys a terminal can't encode. |

Rule of thumb (after crucible's): **C++ owns processes, pixels and persistent
truth; Lua owns editor behavior.**

## Process model

```
┌──────────────────────────── sailfish (C++ host) ─────────────────────────────┐
│ app/       composition root, main loop, supervisor (hot-reload state machine) │
│ platform/  child processes, paths, font discovery (per-OS strategies)         │
│ msgpack/   value type, encoder, streaming decoder                             │
│ rpc/       msgpack-RPC channel (reader/writer threads, request correlation)   │
│ nvim/      Editor interface; Instance = process + channel + UI model          │
│ ui/        UI model: grids, highlights, windows, cursor, cmdline, messages    │
│            redraw-event handlers registered per protocol area                 │
│ render/    FreeType+HarfBuzz text, glyph atlas, SDL_Renderer batching,        │
│            compositor, cursor animation, native cmdline/messages/popupmenu    │
│ input/     SDL keyboard events → Neovim key notation                          │
│ host/      host methods callable from Lua (clipboard, config, ...)            │
│ services/  config history (git), background task runner                       │
└──────────────────────────────────────────────────────────────────────────────┘
        ▲  msgpack-RPC over stdio: redraw events ↓  input + API ↑  rpcrequest ↕
┌──────────────────────── nvim --embed (unmodified) ──────────────────────────┐
│ sailfish runtime (Lua, bundled, loaded with --cmd before the user config)    │
│   boot: host bridge, clipboard provider, config tracer hooks                 │
│   core services: config watch, history, settings, inspector, palette, shop   │
│   features (registry, each can be turned off): sessions, autosave,           │
│   defaults, plugins, languages, ai                                           │
│ user config (~/.config/nvim) — loaded by Neovim exactly as usual             │
└──────────────────────────────────────────────────────────────────────────────┘
```

The host attaches with `ext_linegrid`, `ext_multigrid`, `ext_cmdline`,
`ext_messages` and `ext_popupmenu`:

- **multigrid** gives every window its own grid, so the host knows window
  geometry: floating-window shadows, per-window smooth scrolling.
- **messages/cmdline/popupmenu** arrive as structured events. The host renders
  them natively (no "Press ENTER" prompts, toasts, a message history panel) and
  — critically — sees every error with its *kind* (`emsg`, `lua_error`, ...).
  That is how config errors are detected reliably, with no text scraping.
  If a config installs its own message UI (noice, `ui2`), the runtime tells the
  host, which stops drawing its own while still capturing errors.

## Extension seams

These are the decided extension points; everything else is plain code.

| Seam | Where | Grows when |
|---|---|---|
| Redraw event handlers | `ui/events_*.cpp`, one registration function per protocol area | Neovim adds UI events |
| Host methods (Lua → host) | `host/`; each service registers its own methods | a new host capability is exposed to Lua |
| Runtime entry points (host → Lua) | `host.handle(name, fn)` in Lua; the host calls `host.dispatch` by name, never a module path | the host needs something new from the runtime |
| Render layers | `render/`; a layer per piece of native UI, with draw, key and reset hooks | new native UI |
| Platform strategies | `platform/` (process, font locator) chosen at composition | a new OS |
| Runtime features | `runtime/lua/sailfish/features/*` + registry | a new IDE feature |
| Catalog, essentials, languages | `runtime/lua/sailfish/registry/*.lua` (data) | curated content is added |

## Where things live

| What | Path |
|---|---|
| Neovim config (shared with terminal nvim) | `stdpath("config")`, normally `~/.config/nvim` |
| Sailfish settings (managed Lua table) | `<config>/sailfish.lua` — created on first change |
| All Sailfish data | `~/.local/share/sailfish/` (hard-coded, every OS) |
| ↳ config history (shadow git repos) | `~/.local/share/sailfish/config-history/` |
| ↳ config snapshots (last-known-good checkouts; newest 5 kept) | `~/.local/share/sailfish/snapshots/` |
| ↳ sessions | `~/.local/share/sailfish/sessions/` |
| ↳ catalog plugins (Neovim packages) | `~/.local/share/sailfish/site/pack/sailfish/opt/` |
| ↳ host state, logs (rotated at 4 MB) | `~/.local/share/sailfish/state`, `logs/` |

Neovim's own directories are not relocated, so terminal Neovim and Sailfish
share plugins, shada and undo history — and the language servers and parsers
Sailfish installs, which Mason and nvim-treesitter keep in their usual places
under `stdpath("data")`. The shadow history repo never touches the user's own git
repository (`--git-dir` points into Sailfish data; the work tree is the config).

## Key flows

### Startup

1. Host spawns `nvim --embed`, sets `g:sailfish` (channel id, runtime path,
   mode), then `nvim_ui_attach`.
2. Neovim runs the runtime boot (`--cmd`) → user config → plugins → `VimEnter`.
3. Every `msg_show` of an error kind before the runtime's `startup` report is a
   config error (RPC preserves ordering, so this is deterministic).
4. Clean start → the config's current content is recorded as known-good.
   Errors → a report with *Continue* / *Load last working config* / *Open at error*.

### Hot reload (blue/green instance swap)

Re-sourcing an arbitrary config in place is unsound (lazy.nvim refuses it,
autocmds duplicate, modules stay cached). Sailfish instead:

1. Config file written → runtime notifies host → history records a version.
2. Host waits until the active instance is in plain Normal mode and not
   blocked in a prompt (`nvim_get_mode()`): mid-command, mid-insert or in a
   prompt, the next keys belong to that mode and must not land in a fresh
   editor. Then it asks the instance to export state (session, shada,
   unsaved buffers with undo history) and holds keyboard input from then on.
3. Host starts a candidate instance on the new config, importing that state.
4. Candidate starts cleanly → swap rendering and input to it (held keys
   follow), retire the old one, mark the version good. Errors (including
   ones the runtime's features report in `startup.done`) → retire the
   candidate, keep working in the old instance, show the report.

Every step has a deadline that the main loop's sleep honours (idle check 5 s,
export 10 s, candidate startup 20 s): an instance that doesn't cooperate
cancels the reload rather than freezing the editor. A retired instance that
won't quit gets its input closed, then is killed. The supervisor reaches
processes, history and time through injected ports, so this state machine
is unit-tested against fakes (`tests/test_supervisor.cpp`).

### Sessions and hot exit

Closing the window exports the same state into the workspace's session
directory, so the next launch restores layout, buffers, cursors, undo history
and unsaved edits (VS Code "hot exit"). Autosave reuses this machinery.

## Build

CMake ≥ 3.24, C++20. Dependencies resolve to an installed package when present
and fall back to a pinned source build otherwise: SDL3 (window, input,
clipboard, GPU-backed 2D renderer), FreeType (rasterization), HarfBuzz
(shaping/ligatures), doctest (tests). Neovim ≥ 0.12 is found on `PATH` in
development; release bundles pin and ship it.
