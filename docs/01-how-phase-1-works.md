# How Phase 1 works

A guide to the code behind the editor: which file does what, how the
pieces talk to each other, and the non-obvious decisions (with the reason
for each). Read `00-how-gtk-apps-work.md` first if GObject, signals or the
async/finish pattern are new.

## The big picture

```
PalangApplication                    src/app/palang-application.c
 └ PalangWindow                      src/app/palang-window.c
    ├ sidebar: PalangFileTree        src/workspace/palang-file-tree.c
    │           └ PalangDirectoryModel (one per open folder level)
    └ tabs (AdwTabView), one per open file:
       PalangEditor                  src/editor/palang-editor.c
        ├ PalangSearchBar            src/editor/palang-search-bar.c
        ├ "changed on disk" banner, Go to Line bar
        └ GtkSourceView showing a
          PalangDocument             src/editor/palang-document.c

PalangSettings  (config.ini)         src/settings/palang-settings.c
palang-appearance (light/dark, font) src/settings/palang-appearance.c
PalangSession   (session.ini)        src/app/palang-session.c
palang-save-flow (save + dialogs)    src/app/palang-save-flow.c
```

Each `.ui` file in `data/ui/` holds the layout of the matching C file.

## PalangDocument: one open file

`PalangDocument` *is* a `GtkSourceBuffer` (it subclasses it), so the text
view can show it directly. On top of the buffer it knows where the file
lives and how it's stored (encoding, line endings).

**All loading and saving goes through it.** GtkSourceView's
`GtkSourceFileLoader` / `GtkSourceFileSaver` do the work; the document
wraps them so the rest of the app sees one simple async/finish pair.

Things GtkSourceView does that we work around (each found by a test):

| Behaviour | Why it matters | What we do |
|---|---|---|
| The loader empties the buffer *before* it knows the file is readable | A failed reload would wipe your text | Load into a temporary "scratch" buffer; copy the text over only on success |
| "Implicit trailing newline" hides the last newline and always adds one on save | Files without a final newline would change on every save | Turned off, in `constructed()` (it's a construct property, so setting it in `init()` gets overwritten) |
| A non-UTF-8 byte in the last few bytes of a file is taken as an unfinished UTF-8 character | A Latin-1 file would be shown with a replacement character and saved corrupted | If a UTF-8 load needed a replacement character, retry without UTF-8 |

**Watching the file.** The document keeps a `GFileMonitor` on its file.
Events are coalesced (300 ms) into one check, and nothing is checked while
our own save runs. `GtkSourceFile` remembers the modification time from our
last load/save, so `gtk_source_file_check_file_on_disk()` tells *their*
changes from ours. The document then emits `changed-on-disk(deleted)`;
the editor decides what to do (see below).

## PalangEditor: what's inside a tab

- Connects the view to the document and to the user's settings
  (`bind_settings`: line numbers, tab width… follow changes live).
- Provides `title` ("• main.c" when modified) and `path` properties, which
  the window *binds* to the tab label, tooltip and header bar.
- Hosts the Find bar and the Go to Line bar.
- Reacts to `changed-on-disk`:
  - no unsaved edits → reload silently, cursor stays on its line;
  - unsaved edits → banner with **Reload**; your text is untouched;
  - deleted → banner, and the document is marked modified so closing asks.

## Saving and closing: never lose work

`palang-save-flow.c` is "save this document, asking the user where
needed": the Save As file chooser for a new file, and "File changed on
disk — overwrite?" when another program changed it. It's one GTask whose
reference is handed from step to step (the comment at the top shows the
steps).

Everything that saves uses it: Save, Save As, Save All, and the prompts:

- **Closing a tab** with changes: AdwTabView emits `close-page`; we return
  `GDK_EVENT_STOP` ("we'll decide"), ask, and later call
  `adw_tab_view_close_page_finish (…, TRUE/FALSE)`.
- **Closing the window** (X, Ctrl+Q): `close_request` returns TRUE to keep
  the window open while asking. Once the user decides, we *destroy* the
  window directly — calling `gtk_window_close()` again would be swallowed by
  libadwaita while its dialog is still animating closed (a real bug the
  tests found).
- **Quit** closes each window the normal way, so the prompts apply.

## The folder tree

```
GtkTreeListModel            rows; asks for a folder's children on expand
 └ per folder: GtkSortListModel → GtkFilterListModel → PalangDirectoryModel
```

All levels share one filter (hidden files) and one sorter (folders first,
then names in natural order: `file2` before `file10`).

**Why our own `PalangDirectoryModel`** instead of GTK's
`GtkDirectoryList`: GTK 4.14's version misses files that appear through a
*rename* — which is how many editors save (write a temp file, rename it)
and what `mv` does. Ours reads the folder in batches, then follows every
monitor event, including renames and moves, by adding or removing just that
entry — so expanded folders stay expanded.

## Find / Replace

`GtkSourceSearchSettings` holds what to look for; a
`GtkSourceSearchContext` searches one buffer, highlights all matches and
counts them. The context exists only while the bar is open (dropping it
removes the highlighting). Finding is async; a newer search cancels an
older one through a `GCancellable`.

## Settings and appearance

`PalangSettings` turns each preference into a GObject property, driven by
one table (`options[]` in `palang-settings.c`). Setting a property updates
a `GKeyFile` holding the whole `config.ini` (so unknown sections like a
future `[ai.completion]` survive) and saves it.

Light/dark: libadwaita's `AdwStyleManager` decides. In "Follow System"
mode it follows the desktop's dark-mode preference — but MATE doesn't
report one. So `palang_appearance_choose_color_scheme()` also looks at the
GTK theme name: "Mint-Y-Dark" → dark. It's a pure function, unit-tested
without a display.

The editor font is one display-wide CSS provider for the `palang-editor`
class (`palang_appearance_font_to_css()`).

## Session

When the window closes, `palang_window_dup_session()` captures the folder,
the files (in tab order), the active tab and the sidebar state;
`palang-session.c` writes them to `~/.local/state/palang/session.ini`.
Starting Palang without arguments restores them. Files are reopened one
after another, so the tabs keep their order.

## Two traps worth remembering

1. **C argument evaluation order is unspecified.** In
   `f (data->window, …, g_steal_pointer (&data))` the compiler may run
   `g_steal_pointer` first, setting `data` to NULL before `data->window` is
   read. Read the field into a variable first. (UBSan caught this.)
2. **Don't bind an AdwBin's direct child as a template child.** Both
   `gtk_widget_dispose_template()` and AdwBin free it — a double free. Get
   it with `adw_bin_get_child()` instead (see `palang-file-tree.c`,
   `palang-search-bar.c`).

## Tests

```
meson test -C build            # all of them
meson test -C build window     # one program
```

| Program | Display? | Covers |
|---|---|---|
| `test-document` | no | load/save, encodings, line endings, outside changes |
| `test-settings` | no | config.ini, color-scheme decision, font CSS |
| `test-session` | no | session.ini |
| `test-window` | xvfb | tabs, open/save, the close prompts (by clicking their buttons), reload banner, session restore |
| `test-file-tree` | xvfb | sorting, hidden files, following disk changes |
| `test-editor` | xvfb | find, replace, go to line |
| `test-appearance` | xvfb | light/dark following settings and theme name |

UI tests run under `xvfb-run` (an invisible display) and get their own
config/state folders in `build/test-home/`, so they never touch your real
settings.
