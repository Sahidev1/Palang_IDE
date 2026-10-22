# Module interfaces

What each module offers the rest of Palang, so a session can use a module
without reading its implementation. **The headers are the full contract**
(every public function is documented there); this file says what each
module is for, how the modules connect, and where later phases plug in.

Updated at the end of every phase (see ARCHITECTURE.md §5).
Last updated: end of Phase 2c (2026-10-22).

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
               │     ├ "search":   PalangSearchPanel (search/)
               │     │     "match-activated" → palang_window_open_file_at ()
               │     └ "problems": PalangLspProblemsPanel (lsp/), Ctrl+Shift+M
               │           "problem-activated" → palang_window_open_file_at ()
               ├ PalangPalette (palette/) dialogs: Ctrl+Shift+P, Ctrl+P
               ├ PalangLspManager (lsp/), wired in palang-window-lsp.c
               │     "root" ──bound──▶ manager "folder"
               │     tabs ──▶ add/remove_document (), add_view ()
               │     "server-failed" ──▶ toast; F12/Ctrl+click ──▶
               │     find_definition_async () ──▶ palang_window_open_file_at ()
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
| `palang-document.h` | **PalangDocument**, one open file. It *is* a `GtkSourceBuffer` (use any GtkTextBuffer/GtkSourceBuffer API on it). Async load/save, keeps encoding and line endings, picks the language. Properties: `file` (GFile, NULL if never saved; also notified on every save), `title`. Signals: `changed-on-disk (gboolean deleted)`, for other programs' changes only; `saved`, after our own saves (location and language already updated). |
| `palang-editor.h` | **PalangEditor**, the widget in one tab: the view (`palang_editor_get_view ()` → `GtkSourceView`), the Find bar, Go to Line, the "changed on disk" banner. Properties: `document`, `title` ("• main.c" when modified), `path` ("~/…", for tooltips). |
| `palang-search-bar.h` | The Find/Replace bar inside an editor. Usually reached through `palang_editor_show_search ()` / `palang_editor_find_next ()`. |

- **Rule:** all file I/O goes through `PalangDocument`, never through
  `GtkSourceFileLoader`/`Saver` or GIO directly.
- `palang_editor_go_to ()` also works in a tab opened a moment ago (it
  keeps scrolling until the cursor is visible).
- **Extension points:** `lsp/` (2b) works on the document (text,
  changes via the buffer's `insert-text`/`delete-range` signals, language
  via `gtk_source_buffer_get_language ()`, tags and source marks for
  diagnostics) and on the view (hover provider, gutter mark attributes).
  AI (3) and completion (2c) use the view's
  `gtk_source_view_get_completion ()`.
- **Tag and mark names in use** (don't reuse): `palang-lsp-error`,
  `palang-lsp-warning`, `palang-lsp-info` (tags *and* gutter mark
  categories).
- Tests: `tests/test-document.c`, `tests/test-editor.c`.

## workspace/

| Header | Use it for |
|---|---|
| `palang-file-tree.h` | **PalangFileTree**, the sidebar tree. Properties: `root` (GFile), `show-hidden`. Signal: `file-activated (GFile *file)`. |
| `palang-directory-model.h` | **PalangDirectoryModel**, a live `GListModel` of `GFileInfo` for one folder (follows creates, deletes, renames). Use it instead of GTK's `GtkDirectoryList`, which misses renamed-in files. |

- **The project folder** is the tree's root (`root` property), available
  as `palang_window_get_folder ()`. The terminal, project search and the
  language servers (`PalangLspManager:folder`) follow it by property
  binding.
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

## lsp/

| Header | Use it for |
|---|---|
| `palang-lsp-manager.h` | **PalangLspManager**, the language servers of one window: property `folder`; `add_document ()` / `remove_document ()` for every tab; `add_view ()` for its view (gutter icons, hover); `restart ()`; `get_client ()` / `get_lsp_document ()` for a document; `find_definition_async ()`, `hover_async ()` (Markdown), `complete_async ()`, `resolve_completion_async ()`, `get_problems ()` (live list of every open document's diagnostics). Signal `server-failed (message)`. **Start here.** |
| `palang-lsp-client.h` | **PalangLspClient**, one server process: `start`, `stop` (polite, then killed after 2 s; holds the app), `call_async` (requests), `notify` (notifications), `get_capabilities ()`, `get_sync_kind ()`. Signals `ready` (also after each restart), `failed (message)`, `notification (method, params)`. |
| `palang-lsp-document.h` | **PalangLspDocument**, one document as its server sees it: the sync (didOpen/didChange/didSave/didClose), `get_version ()`, `get_uri ()`, diagnostics (`set_diagnostics ()`, `dup_messages_at ()`). |
| `palang-lsp-diagnostics.h` | Shows diagnostics in a buffer (tags, gutter marks, messages by position); used by PalangLspDocument. |
| `palang-lsp-hover-provider.h` | **PalangLspHoverProvider**, the `GtkSourceHoverProvider` (async): diagnostic messages, then the server's hover text. |
| `palang-lsp-completion-provider.h` | **PalangLspCompletionProvider**, the `GtkSourceCompletionProvider`: asks the server while typing, after its trigger characters and on Ctrl+Space; filters and sorts by what is typed; asks again while the server's list is incomplete. |
| `palang-lsp-completion-item.h` | **PalangLspCompletionItem**, one entry (a `GtkSourceCompletionProposal`): label, detail, kind, documentation (Markdown), fuzzy `match ()`/`compare ()`, `insert ()` into a view (textEdit, additionalTextEdits, snippets with Tab stops, one undo step), `apply_resolved ()`. |
| `palang-lsp-snippet.h` | LSP snippet syntax (`${1:text}`, `$0`, choices, escapes) → parts (text / tab stops), and → plain text. Pure, unit-tested. |
| `palang-lsp-problem.h` | **PalangLspProblem**: one diagnostic as a value (file, line, column, severity, message, source), sorted with `compare ()`. |
| `palang-lsp-problems-panel.h` | **PalangLspProblemsPanel**, the Problems page: a list of PalangLspProblem; signal `problem-activated`. |
| `palang-lsp-markdown.h` | The servers' Markdown → Pango markup for a GtkLabel (code, headings, emphasis; the rest as plain text); `_escape ()` for plain-text answers. Use it for completion documentation too. |
| `palang-lsp-position.h` | Editor characters ↔ LSP UTF-16 units: pure functions and iter wrappers. **Every** position to or from a server goes through these. |
| `palang-lsp-languages.h` | GtkSourceView language id → server (key, title, install hint) and LSP language id. Add a language here. |

- **Servers:** settings `c-server` (clangd), `python-server` (pylsp),
  `javascript-server` (typescript-language-server --stdio), group `[lsp]`;
  empty turns one off. One server per server key per window, started for
  the first document of its language.
- **A server given by its full path** gets its own folder first on its
  `PATH` (nvm keeps `node` next to `typescript-language-server`); that is
  how a server installed with nvm works when Palang starts from the menu.
- **Messages are GVariants** (jsonrpc-glib): JSON objects are `a{sv}`,
  arrays `av` (elements boxed in `v`). Build with `JSONRPC_MESSAGE_NEW`,
  read with `JSONRPC_MESSAGE_PARSE`; `JSONRPC_MESSAGE_PUT_VARIANT` is
  broken in 3.44 (use `GVariantDict`).
- **Rules:** check the document version when an answer comes back, and
  drop it if the text changed (`find_definition_async ()` does). Untitled
  documents aren't sent until saved.
- **Requests about a position** (definition, hover; completion next) go
  through `send_position_request ()` in `palang-lsp-manager.c`: it checks
  the server announced the capability (else `G_IO_ERROR_NOT_SUPPORTED`)
  and remembers the document version; `return_if_stale ()` drops answers
  about old text.
- **Completion answers are not dropped when stale** (the user types on
  while the server thinks); the provider filters them by what was typed
  meanwhile. Fuzzy matching uses GtkSourceView's matcher
  (`gtk_source_completion_fuzzy_match`, lower score is better), not
  `palette/`'s (rule 2: `lsp/` doesn't include `palette/`).
- **Enter rule** (sahi's choice): the popup preselects its first entry, so
  Enter or Tab takes it; but if the word before the cursor already is that
  entry and the selection wasn't moved, Enter starts a new line. The
  provider watches keys from the view's parent (CLAUDE.md gotcha 16).
- **Phase 3 (AI completion)** can be a second `GtkSourceCompletionProvider`
  on the same view, or ghost text beside it; `add_view ()` sets the
  view's completion to `select-on-show` and hides icons. **Tab is already
  taken** three ways: by the completion popup (takes the entry), inside a
  snippet (next place to fill in), else it indents. Accepting ghost text
  with Tab needs a rule for which wins; keys reach a controller on the
  view's parent first (CLAUDE.md gotcha 16).
- **Snippets:** `add_view ()` turns on the view's `enable-snippets` (Tab
  moves through a snippet's places) and empties GtkSourceView's own
  snippet library (CLAUDE.md gotcha 17). The Details pane asks the server
  for documentation (`completionItem/resolve`) when an entry is shown
  there.
- **Problems:** PalangLspDocument emits `diagnostics-changed`; the manager
  rebuilds `get_problems ()` from all documents (positions as they are
  then: servers re-send after edits). The window adds the Problems page
  (`win.show-problems`, Ctrl+Shift+M) in `palang-window-lsp.c`.
- **Phase 3 (ghost text) must not reach servers:** didChange is sent from
  `insert-text`/`delete-range`, so ghost text has to be shown without
  inserting into the buffer, or PalangLspDocument must skip it.
- Tests: `tests/test-lsp.c` with `tests/fake-lsp-server.c` (options list
  at its top).

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
  `win.replace`, `win.find-next`, `win.find-previous`, `win.go-to-line`,
  `win.go-to-definition` (F12), `win.restart-language-servers`,
  `win.show-problems` (Ctrl+Shift+M), `win.show-completions`
  (Ctrl+Space).
- **App stylesheet:** `data/style.css`, loaded by AdwApplication by
  itself (it's in the resource-base-path). Add Palang's own CSS there
  (later: ghost text, math blocks); give widgets a `palang-…` class.
- **Adding a panel page** (e.g. 2c's Problems page): create the
  widget in the window and call `palang_panel_add_page ()`; connect its
  signals there.
- **The window is split over several files** (2b.0): `palang-window.c`
  (tabs, files, saving, session), `palang-window-panel.c` (panel pages,
  key routing), `palang-window-palette.c` (palette, Quick Open),
  `palang-window-lsp.c` (language servers, go to definition), sharing the
  struct through `palang-window-private.h`. New window wiring for a
  feature goes into its own `palang-window-<feature>.c`.
- Tests: `tests/test-window.c` (UI, under xvfb), `tests/test-session.c`,
  `tests/test-application.c`, `tests/test-command-line.c`.

## Testing helpers

- UI tests run under `xvfb-run` with isolated `XDG_*` folders
  (`test_env` in the top `meson.build`). New UI test programs go in the
  `ui_tests` list in `tests/meson.build`.
- `tests/test-window.c` has helpers for real dialogs: `show_window ()`,
  `answer_dialog ()` (clicks a button by its label). See CLAUDE.md
  gotcha 7 for the timing rules.
