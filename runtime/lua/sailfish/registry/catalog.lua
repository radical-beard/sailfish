-- The curated plugin catalog behind :Sailfish shop. Adding a plugin is adding
-- an entry here; nothing else changes.
--
--   id          setting name: require("sailfish").setup({ plugins = { <id> = true } })
--   repo, ref   GitHub repository and the pinned tag or branch
--   module      Lua module the plugin provides; also how Sailfish notices a
--               config that already has the plugin (and then leaves it alone)
--   essential   on by default for a fresh config ("auto")
--   depends     other entries it needs
--   group       at most one entry per group is enabled (themes)
--   setup       sane defaults, run after the plugin loads; its keys are
--               offers (bound only where the config hasn't used the keys)
local map = require("sailfish.keys").offer

return {
  -- Essentials ---------------------------------------------------------------
  {
    id = "telescope",
    name = "Telescope",
    repo = "nvim-telescope/telescope.nvim",
    ref = "v0.2.2",
    module = "telescope",
    category = "Navigation",
    essential = true,
    depends = { "plenary" },
    tags = { "fuzzy", "finder", "picker", "files", "grep", "search" },
    description = "Fuzzy-find files, text, buffers, help and more",
    setup = function()
      require("telescope").setup({})
      local builtin = require("telescope.builtin")
      map("n", "<leader>ff", builtin.find_files, "Find files")
      map("n", "<leader>fg", builtin.live_grep, "Search text in files")
      map("n", "<leader>fb", builtin.buffers, "Find open buffers")
      map("n", "<leader>fh", builtin.help_tags, "Search help")
      map("n", "<leader>fr", builtin.oldfiles, "Recent files")
    end,
  },
  {
    id = "treesitter",
    name = "nvim-treesitter",
    repo = "nvim-treesitter/nvim-treesitter",
    ref = "main",
    module = "nvim-treesitter",
    category = "Languages",
    essential = true,
    tags = { "syntax", "highlighting", "parser", "indent" },
    description = "Parsers and queries for precise highlighting in 300+ languages (installed per language on first use)",
    setup = function()
      require("nvim-treesitter").setup({})
    end,
  },
  {
    id = "lspconfig",
    name = "nvim-lspconfig",
    repo = "neovim/nvim-lspconfig",
    ref = "v2.12.0",
    module = "lspconfig",
    category = "Languages",
    essential = true,
    tags = { "lsp", "language server", "completion", "diagnostics" },
    description = "Ready-made configurations for 400+ language servers",
  },
  {
    id = "mason",
    name = "mason.nvim",
    repo = "mason-org/mason.nvim",
    ref = "v2.3.1",
    module = "mason",
    category = "Languages",
    essential = true,
    depends = { "mason-lspconfig" },
    tags = { "lsp", "installer", "formatter", "linter", "dap" },
    description = "Installs language servers, formatters and linters (used on demand)",
    setup = function()
      require("mason").setup({})
    end,
  },
  {
    id = "mason-lspconfig",
    name = "mason-lspconfig",
    repo = "mason-org/mason-lspconfig.nvim",
    ref = "v2.3.0",
    module = "mason-lspconfig",
    category = "Languages",
    tags = { "lsp", "mason" },
    description = "Maps language-server names between mason and lspconfig",
  },
  {
    id = "blink-cmp",
    name = "blink.cmp",
    repo = "saghen/blink.cmp",
    ref = "v1.10.2",
    module = "blink.cmp",
    category = "Coding",
    essential = true,
    depends = { "friendly-snippets" },
    tags = { "completion", "autocomplete", "snippets", "lsp" },
    description = "Fast completion menu: LSP, paths, buffers and snippets",
    setup = function()
      require("blink.cmp").setup({
        keymap = { preset = "default" },
        fuzzy = { implementation = "prefer_rust" },
        sources = { default = { "lsp", "path", "snippets", "buffer" } },
      })
    end,
  },
  {
    id = "gitsigns",
    name = "gitsigns",
    repo = "lewis6991/gitsigns.nvim",
    ref = "v2.1.0",
    module = "gitsigns",
    category = "Git",
    essential = true,
    tags = { "git", "diff", "hunk", "blame", "signs" },
    description = "Git changes in the sign column; stage, reset and blame hunks",
    setup = function()
      local gs = require("gitsigns")
      gs.setup({})
      map("n", "]h", function() gs.nav_hunk("next") end, "Next git hunk")
      map("n", "[h", function() gs.nav_hunk("prev") end, "Previous git hunk")
      map("n", "<leader>hs", gs.stage_hunk, "Stage hunk")
      map("n", "<leader>hr", gs.reset_hunk, "Reset hunk")
      map("n", "<leader>hb", gs.blame_line, "Blame line")
    end,
  },
  {
    id = "lualine",
    name = "lualine",
    repo = "nvim-lualine/lualine.nvim",
    ref = "master",
    module = "lualine",
    category = "UI",
    essential = true,
    depends = { "devicons" },
    tags = { "statusline", "status", "bar" },
    description = "A fast, good-looking statusline",
    setup = function()
      require("lualine").setup({ options = { theme = "auto", globalstatus = true } })
    end,
  },
  {
    id = "oil",
    name = "oil.nvim",
    repo = "stevearc/oil.nvim",
    ref = "v2.16.0",
    module = "oil",
    category = "Navigation",
    essential = true,
    depends = { "devicons" },
    tags = { "file explorer", "files", "directory", "rename", "tree" },
    description = "Edit directories like buffers: the keyboard-first file explorer (press -)",
    setup = function()
      require("oil").setup({})
      map("n", "-", "<cmd>Oil<cr>", "Open the parent directory")
    end,
  },
  {
    id = "which-key",
    name = "which-key",
    repo = "folke/which-key.nvim",
    ref = "v3.17.0",
    module = "which-key",
    category = "UI",
    essential = true,
    tags = { "keymaps", "help", "discoverability", "popup" },
    description = "Shows what the keys you started typing can do next",
    setup = function()
      require("which-key").setup({})
    end,
  },
  {
    id = "autopairs",
    name = "nvim-autopairs",
    repo = "windwp/nvim-autopairs",
    ref = "0.10.0",
    module = "nvim-autopairs",
    category = "Editing",
    essential = true,
    tags = { "brackets", "pairs", "quotes", "auto close" },
    description = "Closes brackets and quotes as you type",
    setup = function()
      require("nvim-autopairs").setup({})
    end,
  },
  {
    id = "surround",
    name = "nvim-surround",
    repo = "kylechui/nvim-surround",
    ref = "v4.0.5",
    module = "nvim-surround",
    category = "Editing",
    essential = true,
    tags = { "surround", "quotes", "brackets", "tags" },
    description = "Add, change and delete surrounding pairs (ys, cs, ds)",
    setup = function()
      require("nvim-surround").setup({})
    end,
  },
  {
    id = "conform",
    name = "conform.nvim",
    repo = "stevearc/conform.nvim",
    ref = "v9.1.0",
    module = "conform",
    category = "Coding",
    essential = true,
    tags = { "format", "formatter", "prettier", "stylua", "black" },
    description = "Formats code with the right formatter, falling back to the language server",
    setup = function()
      local conform = require("conform")
      conform.setup({ default_format_opts = { lsp_format = "fallback" } })
      map({ "n", "x" }, "<leader>cf", function() conform.format({ async = true }) end, "Format")
    end,
  },

  -- Libraries -------------------------------------------------------------------
  {
    id = "plenary",
    name = "plenary",
    repo = "nvim-lua/plenary.nvim",
    ref = "v0.1.4",
    module = "plenary",
    category = "Libraries",
    tags = { "library", "lua" },
    description = "Lua utilities many plugins build on",
  },
  {
    id = "devicons",
    name = "nvim-web-devicons",
    repo = "nvim-tree/nvim-web-devicons",
    ref = "v0.100",
    module = "nvim-web-devicons",
    category = "Libraries",
    tags = { "icons", "nerd font" },
    description = "File-type icons for other plugins",
    setup = function()
      require("nvim-web-devicons").setup({})
    end,
  },
  {
    id = "friendly-snippets",
    name = "friendly-snippets",
    repo = "rafamadriz/friendly-snippets",
    ref = "main",
    category = "Libraries",
    tags = { "snippets" },
    description = "Snippets for many languages, used by the completion menu",
  },
  {
    id = "nio",
    name = "nvim-nio",
    repo = "nvim-neotest/nvim-nio",
    ref = "v1.10.1",
    module = "nio",
    category = "Libraries",
    tags = { "async", "library" },
    description = "Async I/O library used by debugging and testing plugins",
  },

  -- Navigation -----------------------------------------------------------------
  {
    id = "harpoon",
    name = "Harpoon",
    repo = "ThePrimeagen/harpoon",
    ref = "harpoon2",
    module = "harpoon",
    category = "Navigation",
    depends = { "plenary" },
    tags = { "marks", "bookmarks", "files", "jump" },
    description = "Pin a few files and jump between them instantly",
    setup = function()
      local harpoon = require("harpoon")
      harpoon:setup()
      map("n", "<leader>a", function() harpoon:list():add() end, "Pin file (harpoon)")
      map("n", "<leader>e", function() harpoon.ui:toggle_quick_menu(harpoon:list()) end, "Pinned files")
      for i = 1, 4 do
        map("n", "<leader>" .. i, function() harpoon:list():select(i) end, "Pinned file " .. i)
      end
    end,
  },
  {
    id = "flash",
    name = "flash.nvim",
    repo = "folke/flash.nvim",
    ref = "v2.1.0",
    module = "flash",
    category = "Navigation",
    tags = { "motion", "jump", "search", "labels" },
    description = "Jump anywhere on screen in two keystrokes",
    setup = function()
      local flash = require("flash")
      flash.setup({})
      map({ "n", "x", "o" }, "<leader>j", flash.jump, "Jump (flash)")
    end,
  },
  {
    id = "aerial",
    name = "aerial.nvim",
    repo = "stevearc/aerial.nvim",
    ref = "v4.0.0",
    module = "aerial",
    category = "Navigation",
    tags = { "outline", "symbols", "structure" },
    description = "A code outline: functions, classes and headings at a glance",
    setup = function()
      require("aerial").setup({})
      map("n", "<leader>o", "<cmd>AerialToggle<cr>", "Toggle outline")
    end,
  },
  {
    id = "grug-far",
    name = "grug-far.nvim",
    repo = "MagicDuck/grug-far.nvim",
    ref = "1.6.78",
    module = "grug-far",
    category = "Navigation",
    tags = { "search", "replace", "project", "ripgrep" },
    description = "Project-wide search and replace in a buffer",
    setup = function()
      require("grug-far").setup({})
      map("n", "<leader>sr", "<cmd>GrugFar<cr>", "Search and replace")
    end,
  },

  -- Git ---------------------------------------------------------------------------
  {
    id = "neogit",
    name = "Neogit",
    repo = "NeogitOrg/neogit",
    ref = "v3.0.0",
    module = "neogit",
    category = "Git",
    depends = { "plenary", "diffview" },
    tags = { "git", "magit", "commit", "stage", "branch" },
    description = "A Magit-style git interface: stage, commit, branch, rebase",
    setup = function()
      require("neogit").setup({})
      map("n", "<leader>gg", "<cmd>Neogit<cr>", "Git status (Neogit)")
    end,
  },
  {
    id = "diffview",
    name = "diffview.nvim",
    repo = "sindrets/diffview.nvim",
    ref = "main",
    module = "diffview",
    category = "Git",
    tags = { "git", "diff", "merge", "history" },
    description = "Review diffs, merge conflicts and file history side by side",
    setup = function()
      require("diffview").setup({})
    end,
  },
  {
    id = "fugitive",
    name = "vim-fugitive",
    repo = "tpope/vim-fugitive",
    ref = "v3.7",
    category = "Git",
    tags = { "git", "classic" },
    description = "The classic git wrapper (:Git)",
  },

  -- Coding ------------------------------------------------------------------------
  {
    id = "trouble",
    name = "trouble.nvim",
    repo = "folke/trouble.nvim",
    ref = "v3.7.1",
    module = "trouble",
    category = "Coding",
    tags = { "diagnostics", "problems", "errors", "quickfix" },
    description = "A problems panel for diagnostics, references and quickfix lists",
    setup = function()
      require("trouble").setup({})
      map("n", "<leader>xx", "<cmd>Trouble diagnostics toggle<cr>", "Problems")
    end,
  },
  {
    id = "todo-comments",
    name = "todo-comments",
    repo = "folke/todo-comments.nvim",
    ref = "v1.5.0",
    module = "todo-comments",
    category = "Coding",
    depends = { "plenary" },
    tags = { "todo", "fixme", "comments", "highlight" },
    description = "Highlight and list TODO, FIXME and friends",
    setup = function()
      require("todo-comments").setup({})
    end,
  },
  {
    id = "dap",
    name = "nvim-dap + UI",
    repo = "mfussenegger/nvim-dap",
    ref = "0.10.0",
    module = "dap",
    category = "Coding",
    depends = { "dap-ui" },
    tags = { "debug", "debugger", "breakpoints", "dap" },
    description = "Debugging with breakpoints, stepping and a variables UI",
    setup = function()
      local dap = require("dap")
      map("n", "<F5>", dap.continue, "Debug: continue")
      map("n", "<F9>", dap.toggle_breakpoint, "Debug: toggle breakpoint")
      map("n", "<F10>", dap.step_over, "Debug: step over")
      map("n", "<F11>", dap.step_into, "Debug: step into")
    end,
  },
  {
    id = "dap-ui",
    name = "nvim-dap-ui",
    repo = "rcarriga/nvim-dap-ui",
    ref = "v4.0.0",
    module = "dapui",
    category = "Coding",
    depends = { "nio" },
    tags = { "debug", "ui" },
    description = "Panels for the debugger: scopes, stacks, watches",
    setup = function()
      require("dapui").setup({})
    end,
  },
  {
    id = "treesitter-context",
    name = "treesitter-context",
    repo = "nvim-treesitter/nvim-treesitter-context",
    ref = "v1.0.0",
    module = "treesitter-context",
    category = "Coding",
    tags = { "context", "sticky scroll", "breadcrumbs" },
    description = "Sticky scroll: keep the enclosing function and class in view",
    setup = function()
      require("treesitter-context").setup({})
    end,
  },

  -- UI ------------------------------------------------------------------------------
  {
    id = "indent-blankline",
    name = "indent-blankline",
    repo = "lukas-reineke/indent-blankline.nvim",
    ref = "v3.10.1",
    module = "ibl",
    category = "UI",
    tags = { "indent", "guides", "lines" },
    description = "Indentation guides",
    setup = function()
      require("ibl").setup({})
    end,
  },
  {
    id = "render-markdown",
    name = "render-markdown",
    repo = "MeanderingProgrammer/render-markdown.nvim",
    ref = "v8.14.0",
    module = "render-markdown",
    category = "UI",
    tags = { "markdown", "preview", "render" },
    description = "Renders Markdown headings, tables and code blocks in place",
    setup = function()
      require("render-markdown").setup({})
    end,
  },
  {
    id = "zen-mode",
    name = "zen-mode",
    repo = "folke/zen-mode.nvim",
    ref = "v1.4.1",
    module = "zen-mode",
    category = "UI",
    tags = { "focus", "distraction free", "zen" },
    description = "Distraction-free writing and coding",
    setup = function()
      require("zen-mode").setup({})
      map("n", "<leader>z", "<cmd>ZenMode<cr>", "Zen mode")
    end,
  },
  {
    id = "toggleterm",
    name = "toggleterm",
    repo = "akinsho/toggleterm.nvim",
    ref = "v2.13.1",
    module = "toggleterm",
    category = "UI",
    tags = { "terminal", "shell", "panel" },
    description = "A terminal panel that toggles with Ctrl-\\",
    setup = function()
      require("toggleterm").setup({ open_mapping = [[<C-\>]] })
    end,
  },

  -- AI ----------------------------------------------------------------------------------
  {
    id = "claudecode",
    name = "claudecode.nvim",
    repo = "coder/claudecode.nvim",
    ref = "v0.3.0",
    module = "claudecode",
    category = "AI",
    tags = { "ai", "claude", "agent", "assistant" },
    description = "Claude Code IDE integration: shares selections, shows its diffs for review",
    setup = function()
      require("claudecode").setup({})
      map("n", "<leader>ac", "<cmd>ClaudeCode<cr>", "Toggle Claude Code")
    end,
  },
  {
    id = "copilot",
    name = "copilot.lua",
    repo = "zbirenbaum/copilot.lua",
    ref = "v3.1.11",
    module = "copilot",
    category = "AI",
    tags = { "ai", "copilot", "completion", "ghost text" },
    description = "GitHub Copilot inline suggestions (needs Node.js and a Copilot account)",
    setup = function()
      require("copilot").setup({ suggestion = { auto_trigger = true } })
    end,
  },
  {
    id = "codecompanion",
    name = "CodeCompanion",
    repo = "olimorris/codecompanion.nvim",
    ref = "v19.27.0",
    module = "codecompanion",
    category = "AI",
    depends = { "plenary", "treesitter" },
    tags = { "ai", "chat", "llm", "assistant" },
    description = "Chat with LLMs about your code and apply their edits",
    setup = function()
      require("codecompanion").setup({})
      map({ "n", "v" }, "<leader>cc", "<cmd>CodeCompanionChat Toggle<cr>", "AI chat")
    end,
  },

  -- Themes -----------------------------------------------------------------------------
  {
    id = "tokyonight",
    name = "Tokyo Night",
    repo = "folke/tokyonight.nvim",
    ref = "v4.14.1",
    module = "tokyonight",
    category = "Themes",
    group = "theme",
    tags = { "theme", "colorscheme", "dark" },
    description = "A clean dark theme",
    setup = function()
      vim.cmd.colorscheme("tokyonight")
    end,
  },
  {
    id = "catppuccin",
    name = "Catppuccin",
    repo = "catppuccin/nvim",
    ref = "v2.0.0",
    module = "catppuccin",
    category = "Themes",
    group = "theme",
    tags = { "theme", "colorscheme", "pastel" },
    description = "Soothing pastel theme",
    setup = function()
      vim.cmd.colorscheme("catppuccin")
    end,
  },
  {
    id = "kanagawa",
    name = "Kanagawa",
    repo = "rebelot/kanagawa.nvim",
    ref = "master",
    module = "kanagawa",
    category = "Themes",
    group = "theme",
    tags = { "theme", "colorscheme", "dark" },
    description = "Dark theme inspired by Katsushika Hokusai",
    setup = function()
      vim.cmd.colorscheme("kanagawa")
    end,
  },
  {
    id = "rose-pine",
    name = "Rosé Pine",
    repo = "rose-pine/neovim",
    ref = "v3.0.2",
    module = "rose-pine",
    category = "Themes",
    group = "theme",
    tags = { "theme", "colorscheme" },
    description = "All natural pine, faux fur and a bit of soho vibes",
    setup = function()
      vim.cmd.colorscheme("rose-pine")
    end,
  },
  {
    id = "gruvbox",
    name = "Gruvbox",
    repo = "ellisonleao/gruvbox.nvim",
    ref = "2.0.0",
    module = "gruvbox",
    category = "Themes",
    group = "theme",
    tags = { "theme", "colorscheme", "retro" },
    description = "The retro groove theme",
    setup = function()
      vim.cmd.colorscheme("gruvbox")
    end,
  },
}
