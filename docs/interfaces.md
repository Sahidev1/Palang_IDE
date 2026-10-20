# Module interfaces

What each module offers the rest of Palang, so a session can use a module
without reading its implementation. **The headers are the full contract**
(every public function is documented there); this file says what each
module is for, how the modules connect, and where later phases plug in.

Updated at the end of every phase (see ARCHITECTURE.md §5).
Last updated: end of Phase 1 (2026-10-20).

## How the pieces connect

```
main.c → PalangApplication (app/)
           ├ startup: palang_appearance_follow (palang_settings_get_default ())
           ├ app.* actions + all keyboard shortcuts (one table)
           └ PalangWindow (app/)
               ├ sidebar: PalangFileTree (workspace/)
               │     "file-activated" → palang_window_open_file ()
               ├ AdwTabView, one tab per PalangEditor (editor/)
               │     PalangEditor shows a PalangDocument (editor/)
               ├ win.* actions (file, tabs, find, sidebar)
               └ on close: palang_window_dup_session () → session.ini
```

Settings reach widgets by **property bindings**: e.g. every editor binds its
view's `tab-width` to `PalangSettings:tab-width`, so changes apply live.

## settings/

| Header | Use it for |
|---|---|
| `palang-settings.h` | `palang_settings_get_default ()`: the app-wide preferences. Each preference is a GObject **property** (`color-scheme`, `font`, `tab-width`, `insert-spaces`, `show-line-numbers`, `highlight-current-line`, `wrap-lines`): bind to it or listen to `notify::<name>`. |
| `palang-appearance.h` | Called once at startup; applies light/dark and the editor font. Nothing else needs it. |

- **Adding a preference:** one row in the `options[]` table in
  `palang-settings.c` plus a `PROP_` value; see the comment at the top of
  that file. Unknown groups in `config.ini` survive rewrites, so a later
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

- **The project folder** is the tree's root, available as
  `palang_window_get_folder ()`. Project search (2a), the terminal's
  starting directory (2a) and the LSP workspace root (2b) come from there.
- Tests: `tests/test-file-tree.c`.

## app/

| Header | Use it for |
|---|---|
| `palang-application.h` | Creating the app. Keyboard shortcuts for all actions are in one table, `shortcuts[]` in `palang-application.c`. |
| `palang-window.h` | Open a file or folder, new document, `get_active_editor ()`, `get_n_editors ()`, `get_folder ()`, session capture/restore. |
| `palang-save-flow.h` | `palang_save_document_async ()`: save with the dialogs a user expects (Save As, "changed on disk, overwrite?"). Anything that saves should use it. |
| `palang-session.h` | The `PalangSession` struct and `session.ini` read/write. |
| `palang-preferences-dialog.h` | The Preferences window; each row is bound to a settings property. Add a row there when adding a preference. |

- **Actions:** `app.quit`, `app.preferences`; `win.new`, `win.open`,
  `win.open-folder`, `win.toggle-sidebar`, `win.save`, `win.save-as`,
  `win.save-all`, `win.close-tab`, `win.find`, `win.replace`,
  `win.find-next`, `win.find-previous`, `win.go-to-line`. The command
  palette (2a) can list these: they're the app's commands.
- **Window layout** (`data/ui/palang-window.ui`): `AdwOverlaySplitView`
  with the sidebar on the left; on the right a header bar, the tab bar and
  the tab view (a status page when no tab is open). **There is no bottom
  panel yet**: the terminal (2a) and search results (2a) need one.
- **Gaps for Phase 2:** the window has no public "active editor changed"
  signal (the palette and LSP will want one; `AdwTabView:selected-page`
  is the internal source), and no way for a module to add an action or a
  panel other than editing `palang-window.c`.
- Tests: `tests/test-window.c` (UI, under xvfb), `tests/test-session.c`,
  `tests/test-application.c`.

## Testing helpers

- UI tests run under `xvfb-run` with isolated `XDG_*` folders
  (`test_env` in the top `meson.build`). New UI test programs go in the
  `ui_tests` list in `tests/meson.build`.
- `tests/test-window.c` has helpers for real dialogs: `show_window ()`,
  `answer_dialog ()` (clicks a button by its label). See CLAUDE.md
  gotcha 7 for the timing rules.
