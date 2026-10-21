# Module interfaces

What each module offers the rest of Palang, so a session can use a module
without reading its implementation. **The headers are the full contract**
(every public function is documented there); this file says what each
module is for, how the modules connect, and where later phases plug in.

Updated at the end of every phase (see ARCHITECTURE.md §5).
Last updated: end of Phase 2a (2026-10-21).

## How the pieces connect

```
main.c → PalangApplication (app/)
           ├ startup: palang_appearance_follow (palang_settings_get_default ())
           ├ command table commands[]: shortcuts + palang_application_get_commands ()
           ├ command_line: files to open, or `palang …` subcommands
           │   from the Palang terminal (palang-command-line.c)
           └ PalangWindow (app/)
               ├ sidebar: PalangFileTree (workspace/)
               │     "file-activated" → palang_window_open_file ()
               │     "root" ──bound──▶ terminal "directory", search "folder"
               ├ AdwTabView, one tab per PalangEditor (editor/)
               │     selected tab = "active-editor" property
               ├ PalangPanel (app/) under the tabs, Ctrl+J
               │     ├ "terminal": PalangTerminal (terminal/)
               │     └ "search":   PalangSearchPanel (search/)
               │           "match-activated" → palang_window_open_file_at ()
               ├ PalangPalette (palette/) dialogs: Ctrl+Shift+P, Ctrl+P
               ├ win.* actions
               └ on close: palang_window_dup_session () → session.ini
```

Settings reach widgets by **property bindings**: e.g. every editor binds its
view's `tab-width` to `PalangSettings:tab-width`, so changes apply live.

## settings/

| Header | Use it for |
|---|---|
| `palang-settings.h` | `palang_settings_get_default ()`: the app-wide preferences. Each preference is a GObject **property** (`color-scheme`, `font`, `tab-width`, `insert-spaces`, `show-line-numbers`, `highlight-current-line`, `wrap-lines`): bind to it or listen to `notify::<name>`. |
| `palang-appearance.h` | Called once at startup; applies light/dark and the editor font. Nothing else needs it. |

- **Settings by name, as text** (for the `palang` command):
  `palang_settings_dup_names ()`, `_get_description ()`,
  `_dup_choices ()`, `_dup_allowed_values ()`, `_dup_value ()`,
  `_set_from_string ()`, `_reset ()`; errors in `PALANG_SETTINGS_ERROR`.
- **Adding a preference:** one row in the `options[]` table in
  `palang-settings.c` (with a description) plus a `PROP_` value; see the
  comment at the top of that file. It then also works in `palang set`. Unknown groups in `config.ini` survive rewrites, so a later
  phase can add `[terminal]`, `[lsp]` or `[ai.*]` groups the same way.
- Default style is **dark** ("Follow System" doesn't work on MATE yet; see
  PROGRESS.md, Phase 1).
- Tests: `tests/test-settings.c`, `tests/test-appearance.c`.

## editor/

| Header | Use it for |
|---|---|
| `palang-document.h` | **PalangDocument**, one open file. It *is* a `GtkSourceBuffer` (use any GtkTextBuffer/GtkSourceBuffer API on it). Async load/save, keeps encoding and line endings, picks the language. Properties: `file` (GFile, NULL if never saved), `title`. Signal: `changed-on-disk (gboolean deleted)`, for other programs' changes only. |
| `palang-editor.h` | **PalangEditor**, the widget in one tab: the view (`palang_editor_get_view ()` → `GtkSourceView`), the Find bar, Go to Line, the "changed on disk" banner. Properties: `document`, `title` ("• main.c" when modified), `path` ("~/…", for tooltips). |
| `palang-search-bar.h` | The Find/Replace bar inside an editor. Usually reached through `palang_editor_show_search ()` / `palang_editor_find_next ()`. |

- **Rule:** all file I/O goes through `PalangDocument`, never through
  `GtkSourceFileLoader`/`Saver` or GIO directly.
- `palang_editor_go_to ()` also works in a tab opened a moment ago (it
  keeps scrolling until the cursor is visible).
- **Extension points:** LSP (2b) and AI (3) work on the document (text,
  changes via the buffer's `insert-text`/`delete-range` signals, language
  via `gtk_source_buffer_get_language ()`) and on the view (completion
  providers via `gtk_source_view_get_completion ()`, hover, marks).
- Tests: `tests/test-document.c`, `tests/test-editor.c`.

## workspace/

| Header | Use it for |
|---|---|
| `palang-file-tree.h` | **PalangFileTree**, the sidebar tree. Properties: `root` (GFile), `show-hidden`. Signal: `file-activated (GFile *file)`. |
| `palang-directory-model.h` | **PalangDirectoryModel**, a live `GListModel` of `GFileInfo` for one folder (follows creates, deletes, renames). Use it instead of GTK's `GtkDirectoryList`, which misses renamed-in files. |

- **The project folder** is the tree's root (`root` property), available
  as `palang_window_get_folder ()`. The terminal and project search follow
  it by property binding; the LSP workspace root (2b) should too.
- Tests: `tests/test-file-tree.c`.

## terminal/

| Header | Use it for |
|---|---|
| `palang-terminal.h` | **PalangTerminal**, the user's shell in a VTE terminal. Property `directory` (where the next shell starts); `palang_terminal_set_environment ()` (extra `NAME=value`); starts on first show; `palang_terminal_forward_key ()` for windows whose shortcuts would take the shell's keys; `palang_terminal_get_vte ()` to send input or read the screen. |

- Phase 6 runs `claude` here: `vte_terminal_feed_child ()` types into the
  shell; `PALANG_TERMINAL=1` and Palang's folder first in `PATH` are already
  in the shell's environment.
- Tests: `tests/test-terminal.c`.

## search/

| Header | Use it for |
|---|---|
| `palang-search.h` | **PalangSearch**: `palang_search_start (folder, text, flags)` runs `rg --json`; results as a `GListModel` of PalangSearchMatch; signal `finished (truncated, error_message)`. Also `palang_search_list_files_async ()` (`rg --files`, for Quick Open). |
| `palang-search-match.h` | **PalangSearchMatch**: file, display path, line, first match's column/length (characters), line text, markup with matches in bold. |
| `palang-rg.h` | Pure helpers: `palang_rg_build_argv ()`, `palang_rg_parse_line ()`; `PalangSearchFlags`. |
| `palang-search-panel.h` | **PalangSearchPanel**, the panel's Search page. Property `folder`; signal `match-activated (PalangSearchMatch)`; `palang_search_panel_focus (text)`. |

- `rg` (ripgrep) is required: a `.deb` dependency. Without it, the search
  page and Quick Open say how to install it.
- Searches files on disk: unsaved changes aren't found.
- **Later:** project-wide replace (decided: not in 2a).
- Tests: `tests/test-search.c`.

## palette/

| Header | Use it for |
|---|---|
| `palang-command.h` | **PalangCommand**: action, title, accel; the item type of `palang_application_get_commands ()`. |
| `palang-palette.h` | **PalangPalette** (AdwDialog): type to filter a list of PalangPaletteItem; signal `item-activated` after it closes. `palang_palette_set_items ()` can be called while open; `palang_palette_set_status ()` for "Listing files…" or errors. |
| `palang-palette-item.h` | **PalangPaletteItem**: id, title, subtitle, key (the text typing is matched against). |
| `palang-fuzzy.h` | `palang_fuzzy_match (pattern, text, &score)`: pure, unit-tested. |

- **Reusing the palette** (e.g. Phase 4's math-symbol picker): build
  PalangPaletteItems, present a PalangPalette, act on the id.
- Tests: `tests/test-fuzzy.c`, `tests/test-palette.c`.

## app/

| Header | Use it for |
|---|---|
| `palang-application.h` | Creating the app; `palang_application_get_commands ()`: the command table as a list. Also handles the command line: `palang <files>`, and in the Palang terminal `palang help/settings/get/set/reset`. |
| `palang-command-line.h` | The `palang` subcommands, as text in and text out (`palang_command_line_run ()`). |
| `palang-window.h` | Open a file (also `palang_window_open_file_at (file, line, column)`) or folder, new document, `get_active_editor ()` (also the **`active-editor` property**: listen to `notify::active-editor`), `get_n_editors ()`, `get_folder ()`, `get_panel ()`, `get_terminal ()`, `get_search_panel ()`, session capture/restore. |
| `palang-panel.h` | **PalangPanel**, the bottom panel: `palang_panel_add_page (panel, widget, id, title, icon)`, visible page by id. Show/hide with `gtk_widget_set_visible ()`. |
| `palang-save-flow.h` | `palang_save_document_async ()`: save with the dialogs a user expects (Save As, "changed on disk, overwrite?"). Anything that saves should use it. |
| `palang-session.h` | The `PalangSession` struct and `session.ini` read/write (now also `show-panel`, `panel-height`, `panel-page`). |
| `palang-preferences-dialog.h` | The Preferences window; each row is bound to a settings property. Add a row there when adding a preference. |

- **Adding a command:** install the action (`gtk_widget_class_install_action`
  in the window, or an `app.` action), then add one row to `commands[]` in
  `palang-application.c`: it gets its shortcut and appears in the command
  palette. If its shortcut must work while the terminal has the focus, add
  it to `commands_kept_in_terminal[]` in `palang-window-panel.c`.
- **Actions:** `app.quit`, `app.preferences`; `win.new`, `win.open`,
  `win.open-folder`, `win.toggle-sidebar`, `win.toggle-panel`,
  `win.search-project`, `win.command-palette`, `win.quick-open`,
  `win.save`, `win.save-as`, `win.save-all`, `win.close-tab`, `win.find`,
  `win.replace`, `win.find-next`, `win.find-previous`, `win.go-to-line`.
- **Adding a panel page** (e.g. 2b's list of diagnostics): create the
  widget in the window and call `palang_panel_add_page ()`; connect its
  signals there.
- **The window is split over several files** (2b.0): `palang-window.c`
  (tabs, files, saving, session), `palang-window-panel.c` (panel pages,
  key routing), `palang-window-palette.c` (palette, Quick Open), sharing
  the struct through `palang-window-private.h`. New window wiring for a
  feature (e.g. LSP) goes into its own `palang-window-<feature>.c`.
- Tests: `tests/test-window.c` (UI, under xvfb), `tests/test-session.c`,
  `tests/test-application.c`, `tests/test-command-line.c`.

## Testing helpers

- UI tests run under `xvfb-run` with isolated `XDG_*` folders
  (`test_env` in the top `meson.build`). New UI test programs go in the
  `ui_tests` list in `tests/meson.build`.
- `tests/test-window.c` has helpers for real dialogs: `show_window ()`,
  `answer_dialog ()` (clicks a button by its label). See CLAUDE.md
  gotcha 7 for the timing rules.
