# Phase 2 — Editor essentials

Split into two Claude sessions: **2a** (three fairly independent panels)
and **2b** (the LSP client, big on its own). Each half ends like a phase:
PROGRESS.md entry, manual tests, a `docs/` guide and an updated
`docs/interfaces.md`.

The details below are a starting point; plan them with sahi at the start of
each session, before coding.

## 2a. Terminal, project search, command palette

**Groundwork in `app/`** (first, since all three need it):
- A **bottom panel** in the window (the terminal and search results live
  there), toggled by a shortcut, remembered in the session.
- A public **"active editor changed"** notification on the window (the
  palette needs the current editor; 2b's LSP will too).
- A way to add panels and actions without growing `palang-window.c` much
  (see ARCHITECTURE.md rule 4).

**Integrated terminal** — module `terminal/`
- VTE (GTK4 build, `vte-2.91-gtk4`, installed: 0.76) running the user's
  shell, starting in the project folder.
- Phase 6 runs `claude` in this terminal.

**Project-wide search** — module `search/`
- Use `rg` (ripgrep, installed) through `GSubprocess` when available, with
  a fallback that walks files with `GFileEnumerator` in a worker thread
  (respecting `.gitignore` in a basic way).
- Results in the bottom panel; activating one opens the file at the line
  (`palang_window_open_file ()` + `palang_editor_go_to ()`).

**Command palette** — module `palette/`
- Ctrl+Shift+P; fuzzy search over the app's actions (listed in
  `docs/interfaces.md`, app/) with their shortcuts.

**Verification:** the terminal runs a shell in the project folder; search
finds text across the project and opens results at the right line, with and
without `rg`; the palette runs any action.

## 2b. LSP client

Module `lsp/`.

- `jsonrpc-glib` (installed: 3.44) over the server's stdio via
  `GSubprocess`. GNOME Builder's LSP code is the reference.
- Diagnostics, hover, go-to-definition, popup completions through a
  `GtkSourceCompletionProvider`.
- Servers configured per language (`clangd`, `pyright`, `rust-analyzer`,
  …), in a `[lsp]` group of `config.ini`.
- Works on documents and views through `editor/`'s public interface; the
  window wires it to editors (ARCHITECTURE.md rule 2).
- Language-server types later feed the math type system
  (`phase-4-5-math.md` §7).

**Prerequisite:** no language server is installed yet. Start with C:
`sudo apt install clangd` (sahi runs it).

**Verification:** `clangd`/`pyright` diagnostics and go-to-definition work.
