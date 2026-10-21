# Phase 2 — Editor essentials

Split into two Claude sessions: **2a** (terminal, `palang` command, project
search, command palette) and **2b** (the LSP client, big on its own). Each
half ends like a phase: PROGRESS.md entry, manual tests, a `docs/` guide and
an updated `docs/interfaces.md`.

## 2a. Terminal, `palang` command, project search, command palette

Planned with sahi on 2026-10-21; decisions are marked **(decided)**.

### Steps (one commit each)

| Step | What |
|---|---|
| 2a.1 | Groundwork in `app/`: bottom panel, `active-editor` property, command table |
| 2a.2 | Terminal |
| 2a.3 | `palang` command in the terminal (settings from the command line) |
| 2a.4 | Project search |
| 2a.5 | Command palette + Quick Open |
| 2a.6 | End of phase: PROGRESS, manual tests, `docs/02-*.md`, `interfaces.md` |

### 2a.1 Groundwork in `app/`

- **Bottom panel:** a new type `PalangPanel` (`app/palang-panel.c`): a
  resizable area under the tabs (vertical `GtkPaned`) with switchable pages
  ("Terminal", "Search"). API like `palang_panel_add_page (panel, widget,
  id, title, icon_name)`, so the window only creates the pages and adds
  them. Toggle with **Ctrl+J (decided)**. The session remembers whether it
  is shown, its height and the selected page.
- **`active-editor` property** on `PalangWindow`; others use
  `notify::active-editor` (bindable, more idiomatic than a custom signal).
- **Command table:** `shortcuts[]` in `palang-application.c` becomes one
  table of commands: action, human name ("Save All"), shortcut. It feeds
  both the shortcuts and the palette (as a `GListModel`, so `palette/`
  doesn't depend on `app/`).

### 2a.2 Terminal — module `terminal/`

- `PalangTerminal` wraps a VTE terminal (`vte-2.91-gtk4`). **One terminal
  (decided)**; several (tabs in the panel) can come later.
- Starts the user's shell (`$SHELL`, else from the passwd entry)
  asynchronously, in the project folder (home folder if none).
- When the shell exits: "Process exited — press Enter to restart".
- Ctrl+Shift+C / Ctrl+Shift+V copy and paste; follows light/dark and the
  editor font.
- Environment for the shell: `PALANG_TERMINAL=1`, and `PATH` starting with
  the running Palang's own folder (see 2a.3).
- Phase 6 runs `claude` in this terminal.

### 2a.3 The `palang` command — `app/` + `settings/`

Change preferences from the Palang terminal instead of the Preferences
window. Not intercepted keystrokes: `palang` is the real executable.
Running it again hands the arguments to the running Palang over D-Bus
(GApplication single instance, `G_APPLICATION_HANDLES_COMMAND_LINE`), which
prints its reply into the calling terminal and sets the exit status
(`GApplicationCommandLine`).

```
$ palang help
$ palang settings                 # every setting: value, allowed values, description
$ palang get tab-width
$ palang set tab-width 2          # applies live, saved to config.ini
$ palang set color-scheme purple  # error: must be one of system, light, dark (exit 1)
$ palang reset tab-width          # back to the default
```

- **Only in the Palang terminal (decided):** subcommands work only when
  `PALANG_TERMINAL=1` is set, because outside it a name like `palang set`
  could clash with other uses. Outside, `palang <file>` opens files as
  before (so `palang settings` there opens a file named `settings`).
  Inside, subcommands win; open a file named like a subcommand with
  `palang ./set`. (The variable avoids clashes; it isn't a security
  boundary.)
- **Inside the Palang terminal it always takes priority** over any other
  `palang` on the system: the terminal puts the running Palang's own folder
  first in `PATH` (in development that is `build/src/`, not an installed
  copy).
- **Name: `palang`, no alias (decided).**
- **Generated from the settings table:** each row of `options[]` in
  `palang-settings.c` gains a description; `settings` and `help` are
  produced from the table, so future preferences appear automatically and
  are checked the same way as in Preferences. `settings/` gains reading and
  writing a setting by name as text (e.g.
  `palang_settings_set_from_string (settings, "tab-width", "2", &error)`)
  and a way to list the options.
- Parsing and replies live in `app/palang-command-line.c`.
- **Bash completion (decided):** the `.deb` installs
  `/usr/share/bash-completion/completions/palang`: `palang set <Tab>`
  lists setting names, `palang set color-scheme <Tab>` its values. It
  offers subcommands only when `PALANG_TERMINAL` is set. The setting names
  come from Palang itself (a hidden subcommand), so the script never needs
  updating.

### 2a.4 Project search — module `search/`

- `rg --json` through `GSubprocess`, parsed with json-glib (copes with
  `:` in file names; gives match positions for highlighting).
- **ripgrep is required (decided):** a `.deb` dependency; no fallback file
  walker. If `rg` is missing (a source build), the search page says so.
- UI in the panel: search box with case / whole word / regex options;
  results grouped by file (built: the path above each file's first match,
  since GTK 4.14 section headers hid the first one; see PROGRESS.md); activating a result
  opens the file at its line and column (`palang_window_open_file ()` +
  `palang_editor_go_to ()`). Ctrl+Shift+F opens it with the selection.
- A new search cancels the running one; results are capped (about 5000) so
  huge projects stay responsive.
- Limitation (documented): searches files on disk, so unsaved changes
  aren't found.
- **Project-wide replace: later (decided)**, not in 2a.

### 2a.5 Command palette + Quick Open — module `palette/`

- **Ctrl+Shift+P:** an `AdwDialog` with a search box and the commands
  (name + shortcut) from the command table; Enter runs one.
- **Quick Open, Ctrl+P (decided):** same widget, listing the project's files
  (`rg --files`, which respects `.gitignore`); Enter opens one.
- Fuzzy matching: letters in order, matches at word starts ranked higher
  ("sva" → "Save All"). A pure function, unit-tested without a display.

### Tests

- No display: `rg` JSON parsing, the fuzzy matcher, the command table,
  settings by name as text, `palang` command parsing and replies.
- UI (xvfb): the panel toggles and is restored; the terminal starts a shell
  and shows its output; a search result opens the file at the right line;
  the palette runs an action; Quick Open opens a file.
- Real app with xdotool for each step, including `palang set` typed into
  the terminal changing an open editor.

### Packaging

- Build-Depends: `libvte-2.91-gtk4-dev`, `libjson-glib-dev`.
- Depends (via `${shlibs:Depends}` for the libraries) plus `ripgrep`.
- Installs the bash completion file.

### Verification

The terminal runs a shell in the project folder; `palang set tab-width 2`
in it changes open editors and Preferences, while outside the Palang
terminal `palang set …` is treated as file names; search finds text across the project
and opens results at the right line; the palette runs any action; Quick
Open opens files.

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

**Prerequisite:** `clangd` is installed (checked 2026-10-21). Meson writes
`build/compile_commands.json`, which clangd finds by itself, so Palang's
own sources work as the first test project.

**Verification:** `clangd`/`pyright` diagnostics and go-to-definition work.

### Notes from the size discussion (2026-10-21) — proposals, not decided

Discussed with sahi after 2a; **confirm these with sahi before coding**.

- **Size:** about as much code as 2a (~4,000–5,000 lines with tests) but
  1.5–2× the effort: one deep asynchronous feature instead of five small
  ones, and its bugs are less visible.
- **Proposed split into two sessions:**
  - **2b:** step 0 (split `palang-window.c`, see PROGRESS.md 2a open
    items), client core (spawn per language + project folder, `initialize`,
    shutdown, restart after a crash, `[lsp]` settings), document sync
    (didOpen/didChange/didSave/didClose), diagnostics (underlines, gutter,
    message on hover), go to definition (F12 / Ctrl+click →
    `palang_window_open_file_at ()`).
  - **2c:** hover popups and completion (`GtkSourceCompletionProvider`,
    trigger characters, snippets). Completion sits next to Phase 3's ghost
    text, so doing it last fits the roadmap.
- **Rough parts and sizes:** client core 700–900 lines, sync 400,
  diagnostics 500–700, hover 250, go to definition 200, completion
  600–800, tests 700–900.
- **Traps to plan for:**
  - LSP positions count **UTF-16 code units**; the editor counts
    characters. Every position needs converting (emoji and other
    characters outside the BMP are where it shows).
  - Replies can arrive after the document changed; use the document
    version and drop stale replies.
  - Tests need a **fake language server** (a small program speaking
    JSON-RPC over stdio), so they don't depend on clangd.
- **Open questions for sahi:**
  1. Python server: `pyright` needs npm; `python3-pylsp` comes from apt
     (`sudo apt install python3-pylsp`, not installed yet). Proposal: pylsp.
  2. A "Problems" page in the bottom panel listing all diagnostics: in 2b,
     or later?
  3. Agree to the 2b/2c split (PLAN.md roadmap would get a 2c row).
