# Roadmap

Candidates for what Sailfish does next, judged against its constraints:
keyboard-only, fully compatible with existing Neovim configs, configured in
Lua, and nothing installed until it's needed. Effort is relative size (S, M,
L), not time.

## Already in 0.1

Modern-IDE features that Neovim doesn't have natively and Sailfish now
provides: command palette, hot exit, VS Code-style autosave, config hot reload
with automatic rollback, config history, a settings screen, a curated plugin
shop, languages installed on first use, native notifications (toasts),
smooth scrolling, font zoom, AI chat / inline edit / explain, and a config
inspector (provenance, keymap conflicts, startup cost).

## Gaps before 1.0

| Gap | Today | Fix | Effort |
|---|---|---|---|
| Packaging | Built from source. | A macOS `.app`, Linux AppImage and Windows installer that bundle a pinned Neovim. | M |
| GUI launch environment | Launched from Finder/Dock, the app doesn't inherit the shell's `PATH` and variables, so `nvim`, `git`, language servers and `ANTHROPIC_API_KEY` may be missing. | Capture the login shell's environment at startup (`$SHELL -l -i -c env`), as VS Code does. | S |
| Linux and Windows | Written (posix/fontconfig, Win32/DirectWrite) but never compiled. | CI building and testing on all three OSes. | M |
| Crash recovery | If Neovim crashes the window closes; the next launch restores the session saved ≤ 30 s earlier. | Relaunch immediately into the last session and say what happened. | S |
| IME composition | Composition is tracked (keys aren't sent mid-composition) but the preedit text isn't drawn. | Draw preedit at the cursor and position the IME candidate window. | S |
| Color emoji | Drawn only when FreeType comes from the system (`brew install freetype`); the fallback source build has no PNG support, which sbix/CBDT emoji fonts need. | Fetch libpng with the other pinned dependencies. | S |
| `:restart` and `:connect` | A typed `:restart` and `ZR` run a hot reload instead, which does the same job. A `:restart` issued from code (or with `!`/`+cmd`) still closes the window and, as Neovim documents for UIs that don't reattach, leaves its new server running. | Follow Neovim's `restart`/`connect` events by attaching to the new server over its socket: a socket transport for `nvim::Instance` (see Remote development). | M |
| Safe mode environment | Safe mode points `XDG_CONFIG_HOME` at the snapshot, so terminals, git and language servers started from it see the snapshot as `~/.config`. | Load the snapshot through `-u` and `'runtimepath'` instead, without changing `stdpath("config")` semantics for plugins. | M |

## Candidates

### Strong fit — recommended next

| Feature | What | Why it fits | Effort |
|---|---|---|---|
| GUI-chord defaults | Cmd+P quick open, Cmd+Shift+F search in files, Cmd+Shift+O symbols, Cmd+B file explorer, Cmd+S save, Cmd+W close buffer (Ctrl+Shift elsewhere), bound only when free. | Terminals can't send these chords, so they never collide with Vim habits — pure gain for people arriving from VS Code. | S |
| Breadcrumbs and sticky scroll | A winbar showing the symbol path from LSP document symbols; treesitter-context on by default. | Orientation in large files without a mouse. | S |
| Inlay hints by default | Turn on `vim.lsp.inlay_hint` (and code lens where servers offer it) for servers Sailfish set up. | Already in Neovim; only the default is missing. | S |
| Keybinding editor | Search every key, see conflicts (the inspector already finds them) and rebind from the UI into `sailfish.lua`. | "Change config without knowing where everything is", for the setting people change most. | M |
| Inline AI completions | Ghost-text completions and next-edit suggestions from a fast model (Claude Haiku 4.5), debounced, accepted with a key. | The most-used AI feature in modern editors; fits the AI-first goal. | M |
| Agent mode | Claude makes multi-file changes with tools (read, grep, run tests); every file's diff is reviewed hunk by hunk before it lands, reusing the edit review UI. Alternatively, a first-class Claude Code integration (claudecode.nvim is already in the shop). | Bigger AI tasks without leaving the editor, with the same "nothing changes until you've reviewed it" rule as `:Sailfish edit`. | L |
| Problems panel | Workspace diagnostics in a native panel, grouped by file, with jump and quick-fix. | Every IDE has one; trouble.nvim exists but isn't on by default. | S |
| Test explorer | neotest plus adapters installed per language on first use, like language servers. | Run and debug the test under the cursor, see results inline. | M |
| Debugging out of the box | nvim-dap with adapters installed on first use (Mason), launch configurations detected per project, `launch.json` import. | The largest remaining gap between Neovim and VS Code. | L |

### Good fit — worth considering

| Feature | What | Effort |
|---|---|---|
| Remote development | The GUI runs locally, Neovim on a server over SSH; the protocol already runs over TCP. Needs the runtime shipped to the remote and port forwarding. | L |
| Detachable windows | Move a Neovim window or the Claude panel into its own OS window; multigrid already gives each window its own grid. | M |
| Rich content | Images in Markdown, hover docs and notebooks, drawn by the host — something terminals can't do reliably. | M–L |
| Screen reader support | Expose the buffer, cursor and messages through the platform accessibility APIs. Terminal Neovim is nearly unusable with a screen reader; this matters for a keyboard-only IDE. | L |
| Settings sync | Push the config history to a git remote of the user's choosing and pull it on another machine. | S–M |
| Import from VS Code | Map installed extensions and common settings to shop entries and options. | M |
| Workspace trust | A prompt and a list in settings over Neovim's `exrc` + `vim.secure` trust database. | S |
| Native tabline | Tabs drawn by the host with file icons and modified markers (`ext_tabline`). | S |

### Poor fit — not recommended

- **Minimap** — a mouse-era navigation aid; keyboard users navigate by search,
  marks and symbols. It's also expensive here: with multigrid the host only
  sees visible rows.
- **Drag-and-drop layout editing** — mouse-only by nature.
- **Live collaboration** (Live Share) — needs a relay service and a CRDT layer
  over Neovim buffers; large cost for a niche need.
- **A second built-in terminal** — Neovim's `:terminal` is already good;
  toggleterm in the shop covers the panel workflow.
