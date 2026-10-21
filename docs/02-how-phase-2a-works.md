# How Phase 2a works

A guide to the code added in Phase 2a: the bottom panel, the terminal, the
`palang` command, project search, and the command palette with Quick Open.
It explains which file does what, how the pieces connect, and the
non-obvious decisions with their reasons. Phase 1's guide
(`01-how-phase-1-works.md`) covers everything older.

## The big picture

```
PalangApplication                     src/app/palang-application.c
 ├ command table (commands[])         → shortcuts + palette list
 ├ command_line: subcommands          src/app/palang-command-line.c
 └ PalangWindow                       src/app/palang-window.c
    ├ sidebar: PalangFileTree ─ root ─┬─▶ terminal "directory"
    │                                 └─▶ search panel "folder"
    ├ tabs ─ "active-editor" property
    └ GtkPaned (vertical)
       ├ tabs (as in Phase 1)
       └ PalangPanel                  src/app/palang-panel.c
          ├ "terminal": PalangTerminal      src/terminal/palang-terminal.c
          └ "search":   PalangSearchPanel   src/search/palang-search-panel.c
                         └ PalangSearch (runs rg)  src/search/palang-search.c

PalangPalette (dialog)                src/palette/palang-palette.c
 ├ Ctrl+Shift+P: items from the command table
 └ Ctrl+P: items from `rg --files` (palang_search_list_files_async)
```

New modules: `terminal/`, `search/`, `palette/`. They depend only on
`settings/` (the terminal, for its font); the window (`app/`) creates them
and connects them, following ARCHITECTURE.md rule 2. For example, the
search panel doesn't open files: it emits `match-activated`, and the window
opens the file.

## The bottom panel

`PalangPanel` is a switcher row plus an `AdwViewStack` of pages. The window
adds the pages (`palang_panel_add_page`) and puts the panel under the tabs
in a vertical `GtkPaned` (`data/ui/palang-window.ui`). Ctrl+J
(`win.toggle-panel`), the header bar button and the panel's × all just
change the panel's `visible` property.

**Its height is the window's job**, because the `GtkPaned` decides it.
GtkPaned measures the divider's position from the top, but what we want to
keep is the panel's height. So:

- `panel_height_on_screen ()` = paned height − divider position. That is
  what the session saves (`panel-height`) and what is remembered when the
  panel hides.
- When the panel is **shown**, `restore_panel_height ()` moves the divider
  back. It runs as a *tick callback* (before the next frame), because
  right after startup the paned has no height yet. Without this, GtkPaned
  gives a newly shown panel its *minimum* height, and window resizes while
  it was hidden would have moved the divider.
- An earlier attempt measured the panel itself and lost a pixel (the
  divider's handle) on every hide/show. Measuring from the divider avoids
  that.

**Focus:** showing the panel moves the keyboard focus into its page
(`palang_panel_focus_page`, which uses `gtk_widget_child_focus` to find
the first focusable widget). Hiding it while it had the focus gives the
focus back to the text.

## The command table

`commands[]` in `palang-application.c` lists every command once: action,
name for people, shortcut. Startup turns it into keyboard shortcuts, and
`palang_application_get_commands ()` offers it as a `GListModel` of
`PalangCommand`. That type lives in `palette/`, so the palette never
includes an `app/` header. **To add a command:** install the action in the
window (or app) and add one row to the table; it then has a shortcut and
appears in the palette.

## The terminal

`PalangTerminal` wraps a `VteTerminal` (the GNOME Terminal widget). Its
shell is `$SHELL`, else the account's shell, else `/bin/sh`. It starts on
the terminal's first `map` (when it's first shown), so a hidden terminal
costs nothing. It starts in the project folder: the window binds the file
tree's `root` to the terminal's `directory`, which is used the next time a
shell starts.

- **Exit and restart:** VTE's `child-exited` prints a dimmed note
  ("Process exited — press Enter to restart"). The terminal's own key
  controller starts a new shell on Enter.
- **Copy/paste:** Ctrl+Shift+C/V, because Ctrl+C belongs to the shell.
- **Looks:** colors follow libadwaita's dark flag and the font follows the
  editor font setting, through `g_signal_connect_object ()`. That variant
  disconnects automatically when the terminal is freed, which matters
  because the style manager and the settings live longer than any terminal.

**Who gets a key while the terminal has the focus?** In GTK4, the
application's shortcuts act before the focused widget ever sees the key. So
at first, Ctrl+W in bash closed an editor tab instead of deleting a word.
The fix is in the window: a key controller added in `palang_window_init`
(since 2b.0: `_palang_window_init_key_routing` in
`src/app/palang-window-panel.c`) runs before the app's shortcuts. When the focus is in the terminal, it
hands the key to the terminal (`palang_terminal_forward_key`, which uses
`gtk_event_controller_key_forward`) and stops it there. Only the commands
in `commands_kept_in_terminal` (toggle panel, sidebar, project search,
palette, Quick Open, Preferences, Quit) keep their shortcuts in the
terminal. Note that this takes **Ctrl+P** (bash's "previous command") for
Quick Open, as VS Code does; the Up arrow does the same in bash.

**Freeing:** VTE reports the shell's exit *while it is being destroyed*,
after `PalangTerminal` is gone. ASan caught the resulting use-after-free.
`dispose` now disconnects from VTE first.

## The `palang` command

The plan was "`palang` is the real executable, not intercepted keys". It
works like this:

1. The terminal gives its shell `PALANG_TERMINAL=1` and puts the running
   Palang's own folder first in `PATH` (read from `/proc/self/exe`). So
   `palang` in the Palang terminal is always *this* Palang (`build/src/`
   during development).
2. Typing `palang set tab-width 2` starts a second Palang process. Thanks
   to `G_APPLICATION_HANDLES_COMMAND_LINE`, GApplication doesn't run
   anything in it: it sends the arguments to the running Palang over
   D-Bus, and `palang_application_command_line ()` runs there.
   **`G_APPLICATION_SEND_ENVIRONMENT`** is needed too; without it the
   caller's environment (and so `PALANG_TERMINAL`) doesn't arrive.
3. If the caller's `PALANG_TERMINAL` is `1` and the first word is a
   subcommand, `palang_command_line_run ()` runs it and the reply is printed
   in the calling terminal (`g_application_command_line_print_literal`).
   The function's return value becomes the caller's exit status. Otherwise
   the arguments are files to open, as before.

`palang-command-line.c` has its own table of subcommands; `help` is
generated from it. `settings` is generated from the settings table in
`palang-settings.c`, which now has a description for each option and
functions to read and write a setting by name as text
(`palang_settings_set_from_string`, …). A new preference therefore works
in `palang set` with no extra code, and is checked exactly as in
Preferences.

**Bash completion** (`data/palang.bash-completion`, installed by the
`.deb`) asks the running Palang for the words: `palang __complete
subcommands | names | values NAME`. The script never needs updating.

## Project search

Three layers, from pure to UI:

- **`palang-rg.c`** (pure, no process): builds rg's arguments and parses
  one line of `rg --json` into a `PalangSearchMatch`. Two traps: rg's match
  positions are **bytes** while the editor counts **characters**
  (`char_offset ()` converts), and text that isn't valid UTF-8 arrives
  base64-encoded as `{"bytes": …}` instead of `{"text": …}`.
- **`PalangSearch`** runs rg (`GSubprocess`) and reads its output one line
  at a time, asynchronously. Each search is a `Run` struct passed through
  the callbacks. A new search cancels the old run's `GCancellable`, and its
  next callback frees it, so old results can't mix with new ones. Matches
  go into the results list in batches of 100, and rg is stopped at 5000.
  Exit status 2 means "error" (e.g. a bad regex); its stderr's last line
  becomes the message. `--sort path` keeps the order (and which results
  are left out past the limit) the same on every run.
- **`PalangSearchPanel`**: search box, toggles, status line and the list.
  The file's path is shown above each file's first match
  (`starts_a_file ()`). The plan said GTK section headers, but in GTK 4.14
  the list kept the first header scrolled just out of view, and neither
  resetting the adjustment nor `gtk_list_view_scroll_to` helped.

Search looks at the files **on disk**, so unsaved changes aren't found.

**Opening a result** goes through `palang_window_open_file_at (file, line,
column)`, a new public function (also useful for LSP's go-to-definition
in 2b). Writing it exposed a Phase 1 bug: in a tab opened a moment ago,
`palang_editor_go_to ()` put the cursor on the line but didn't scroll
there. A text view learns its height, and then its lines' heights, over its
first frames. `keep_scrolling_to_cursor ()` now retries on each frame until
the cursor is visible (for about a second at most).

## Command palette and Quick Open

`PalangPalette` is an `AdwDialog` with a search box over a list of
`PalangPaletteItem` (title, subtitle, the text to match, an id). It knows
nothing about commands or files: picking an item closes the dialog and
emits `item-activated`, and the window acts on the id. For commands the id
is the action name (`gtk_widget_activate_action` also finds `app.`
actions); for Quick Open it is the file's path relative to the folder.

**Fuzzy matching** (`palang-fuzzy.c`) is a pure function. Letters must
appear in order (ignoring case). Points are given for word starts (after
space, `-`, `_`, `.`, `/`, and camelCase) and for consecutive letters, and
gaps cost a point each. It tries every place the pattern's first letter
occurs, because taking only the first one would match "pan" against the
"pa" of "**pa**lang-panel.c" instead of "**pan**el". Equal scores are
sorted shorter first, then in the original order.

Quick Open lists files with `rg --files --sort path`, so `.gitignore`d and
hidden files are left out, like in search. The palette opens right away
with "Listing files…" and fills in when rg is done.

## Tests

| Program | Display? | Covers |
|---|---|---|
| `test-command-line` | no | every subcommand, errors and exit status, completion words |
| `test-search` | no | rg arguments, JSON parsing (bytes, UTF-8), real searches: case, regex, invalid regex, the 5000 cap, restarting |
| `test-fuzzy` | no | matching and ranking |
| `test-settings` | no | + settings by name as text |
| `test-session` | no | + panel fields |
| `test-application` | no | + the command table |
| `test-terminal` | xvfb | starts when shown, folder and environment, exit and restart |
| `test-palette` | xvfb | filtering, ordering, Enter picks |
| `test-window` | xvfb | + panel toggle and height, active-editor, terminal folder, open at a line in a new tab, project search and Quick Open end to end |

Searching tests need `rg`; they are skipped without it. Key handling (which
keys reach the shell, Up/Down in the palette, copy/paste) needs real key
presses: see MANUAL_TESTS.md, Phase 2a.
