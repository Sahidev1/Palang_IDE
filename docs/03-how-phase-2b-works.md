# How Phase 2b works

A guide to the code added in Phase 2b: the language server client
(module `lsp/`), how it keeps servers in step with the editor, the
diagnostics, and go to definition. It explains which file does what, how
the pieces connect, and the non-obvious decisions with their reasons.
Phase 2a's guide (`02-how-phase-2a-works.md`) covers everything older.

## The big picture

```
PalangWindow                              src/app/palang-window.c
 │  (window code split over palang-window-*.c, see palang-window-private.h)
 └ palang-window-lsp.c
    ├ file tree "root" ──bound──▶ manager "folder"
    ├ tab added/removed ──▶ palang_lsp_manager_add/remove_document ()
    │                      palang_lsp_manager_add_view ()
    ├ "server-failed" ──▶ toast with Details
    └ F12 / Ctrl+click ──▶ palang_lsp_manager_find_definition_async ()
                           ──▶ palang_window_open_file_at ()

PalangLspManager (one per window)         src/lsp/palang-lsp-manager.c
 ├ clients: "c" → PalangLspClient (clangd), "python" → … (pylsp), …
 │   PalangLspClient                      src/lsp/palang-lsp-client.c
 │    └ GSubprocess (the server) + JsonrpcClient on its stdin/stdout
 └ documents: one PalangLspDocument per open tab
     PalangLspDocument                    src/lsp/palang-lsp-document.c
      ├ didOpen / didChange / didSave / didClose to its client
      └ PalangLspDiagnostics               src/lsp/palang-lsp-diagnostics.c
         (underline tags, gutter marks, messages, in the PalangDocument)

PalangLspHoverProvider (one per view)     src/lsp/palang-lsp-hover-provider.c
palang-lsp-position.c   editor characters ↔ LSP UTF-16 units
palang-lsp-languages.c  GtkSourceView language → server and LSP id
```

`lsp/` depends on `editor/` (documents and views) and `settings/` (the
server commands), as ARCHITECTURE.md rule 2 allows. It knows nothing about
the window: it reports failures with a signal, and go to definition
returns a file and position that the window opens.

## A language server in one paragraph

A language server is a separate program (clangd, pylsp, …) that
understands one language. The editor starts it, and they talk **JSON-RPC**
over the server's stdin and stdout: each message is a JSON object with a
`Content-Length:` header in front. There are *requests* (with an id; the
other side answers) and *notifications* (no answer). The editor tells the
server which files are open and every edit to them; the server sends back
diagnostics on its own, and answers questions like "where is this
defined?". Positions are line + character, both from 0, with characters
counted in **UTF-16 code units** (see "Positions" below).

jsonrpc-glib (GNOME Builder's library) does the framing and the
JSON ↔ GVariant conversion: a JSON object arrives as an `a{sv}`
dictionary, an array as `av`. `JSONRPC_MESSAGE_NEW (…)` builds a message
and `JSONRPC_MESSAGE_PARSE (…)` reads one. (`JSONRPC_MESSAGE_PUT_VARIANT`
doesn't work in the installed 3.44; we insert sub-objects with
`GVariantDict` instead.)

## The client: one server's life

`PalangLspClient` owns one server process. Its states:

```
STOPPED ──start──▶ STARTING ──"initialize" answered──▶ READY
                      │  ▲                               │
                      │  └──── crashed, start again ◀────┤
                      ▼                                  ▼
                    FAILED                          STOPPING ──exited──▶ STOPPED
```

- **Starting:** `GSubprocessLauncher` with stdin/stdout pipes (stderr
  thrown away unless `PALANG_LSP_STDERR=1`), working directory = project
  folder. Then the `initialize` request (who we are, the folder, what we
  support), and after its answer the `initialized` notification. Only
  then is the client READY and emits `ready`.
- **Requests the server sends us** (`workspace/configuration`,
  `window/workDoneProgress/create`, …) all get an answer, so the server
  never waits for us.
- **Crashes:** the process exiting without being asked counts as a
  crash; up to 3 restarts within a minute, then FAILED with "keeps
  stopping". A missing program (`G_SPAWN_ERROR_NOENT`) or an *error answer*
  to `initialize` fails at once: retrying wouldn't help, and the server's
  own message ("Could not find a valid TypeScript installation") is the
  useful one.
- **Stopping:** `shutdown` request, then `exit` notification, and
  `g_subprocess_force_exit` if it hasn't exited after 2 s. The client keeps
  a reference to itself and **holds the application**
  (`g_application_hold`), so quitting Palang waits up to those 2 s instead
  of leaving servers behind.
- **Callbacks that don't hold a reference:** each run of the server has a
  `GCancellable`. Ending the run cancels it, so callbacks still pending for
  that run get `G_IO_ERROR_CANCELLED` and return without touching the
  client, which may be gone. That's why the process-exit and initialize
  callbacks get a plain pointer.

## The manager: which servers run

`PalangLspManager` has two collections: the clients (server key →
client) and the documents (one `PalangLspDocument` per open tab).
`ensure_client ()` looks at a document's language
(`palang_lsp_find_server ()` in `palang-lsp-languages.c`), starts the
server if needed, and hands the document to it. That runs when a
document is added, when its language changes (Save As `notes.txt` →
`notes.py`), and for all documents after anything that changes the set of
servers:

- **the folder** (a server serves one folder): every server is stopped
  and started again;
- **a server's setting** (`palang set python-server …`, or Preferences
  later): that server only;
- **"Restart Language Servers"**: all, including failed ones.

Before a client is stopped, its documents are taken off it (`didClose`).
A **failed** client stays in the table, so a missing server is reported
once rather than for every file; restarting clears it. A server keeps
running when its last file closes, since opening another one is likely
and starting can take seconds.

**Settings:** `c-server`, `python-server`, `javascript-server` in the
`[lsp]` group, ordinary rows in the settings table, so `palang
set/get/reset` work. The value is a command line split like a shell would
(`g_shell_parse_argv`); empty turns the server off. The install hint in a
"not installed" message is shown only when the command is the default
one.

## Document sync

`PalangLspDocument` keeps the server's copy of one document's text equal
to the buffer:

- **didOpen** with the whole text, when the client is READY, the document
  has a file (servers name documents by URI; untitled ones wait until
  saved) and a language the server knows. Also after every restart: the
  client's `ready` signal makes the document open itself again.
- **didChange** for every edit. The buffer's `insert-text` and
  `delete-range` signals are connected with plain `g_signal_connect`,
  which runs *before* GTK changes the text, so the iterators still
  describe the old text — exactly what the protocol wants: "replace this
  range of what you have with this". Servers that asked for the whole
  text (`textDocumentSync` = 1) get it from `changed`, after the edit.
- **didSave** from the new `PalangDocument::saved` signal.
- **didClose** when the tab closes, the document moves to another server,
  or Save As gives it a new name (close the old name, open the new one).
  `set_client ()` compares the URI and language it opened with against
  the current ones, so a plain save (which also notifies `file`) doesn't
  reopen anything.

The **version** grows with every change and never goes back. Answers about
an older version are stale: diagnostics for an older version are dropped,
and a definition that arrives after the text changed is dropped too (the
server sends new diagnostics anyway; for a definition the user has moved
on).

## Positions

GTK counts characters (code points), LSP counts UTF-16 code units, and
they differ for characters above U+FFFF (emoji, mathematical letters like
𝑥), which take two units. `palang-lsp-position.c` has two pure functions
(`palang_lsp_utf16_length`, `palang_lsp_chars_for_utf16`) and two
wrappers for iters. They use `gtk_text_iter_get_slice`, which includes
text hidden by tags: the server must see all of the text, which matters
for Phase 5's hidden code.

Every position to or from a server goes through these. The one exception:
a definition in a file that **isn't open** — we can't convert without its
text, so its UTF-16 column is used as a character column (wrong only if an
emoji or similar comes before it on that line).

## Diagnostics

`textDocument/publishDiagnostics` arrives as a client `notification`; the
manager finds the document by URI and calls
`palang_lsp_document_set_diagnostics ()`. `palang-lsp-diagnostics.c`
then replaces what's shown:

- **Underline:** a `GtkTextTag` per severity with `underline =
  PANGO_UNDERLINE_ERROR` (the wavy one) and a colour: red errors, orange
  warnings, blue information and hints. An empty range is widened to one
  character so it shows.
- **Gutter icon:** a `GtkSourceMark` whose *category* is the same name
  as the tag; each view gets `GtkSourceMarkAttributes` for the three
  categories (icon, priority) in `palang_lsp_diagnostics_setup_view ()`,
  and `show-line-marks` on. The column is always there, so text doesn't
  jump sideways when the first error appears.
- **Message:** each diagnostic keeps two `GtkTextMark`s (start, end) and
  its text. Marks move with edits, so until the server sends new
  diagnostics everything stays on the right text.

Clearing removes the tags and gutter marks over the whole buffer and
deletes the text marks. It happens on new diagnostics, on didClose, and
when the server restarts.

**Hover:** `PalangLspHoverProvider` implements GtkSourceView's
`GtkSourceHoverProvider` interface (a GObject *interface*: our type
promises to provide `populate`, which GtkSourceView calls when the pointer
rests on the text). It finds the document through the manager (a weak
pointer, so a provider never uses a manager that's gone) and shows the
messages at the position in a selectable label. GtkSourceHover gives the
character boundary *nearest* the pointer, so a range's end counts as
inside — otherwise the right half of a one-letter error like `x` showed
nothing. GtkSourceView 5 has no tooltips for gutter icons.

## Go to definition

`palang_lsp_manager_find_definition_async ()` sends
`textDocument/definition` for a position and reads the answer, which may
be one `Location` (`uri` + `range`), an array of them (we take the
first), `LocationLink`s (`targetUri` + `targetSelectionRange`, the
name's position) or null ("nothing found"). It returns a file, line and
column counted from 0 in characters; the window adds 1 and calls
`palang_window_open_file_at ()`.

In the window, **F12** (`win.go-to-definition`) uses the cursor. **Ctrl+click**
is a `GtkGestureClick` on each view in the *capture* phase (so it sees the
click before the text view does): with Ctrl it is *claimed* (the view
doesn't start a selection), the cursor moves to the clicked place, and
the same lookup runs; without Ctrl it is *denied* and the view handles
the click as usual.

This exposed an old bug in `palang_editor_go_to ()` for a tab opened a
moment ago: a new text view's line positions are estimates for a few
frames, so "the cursor is visible" could be true and then stop being
true. It now waits until the cursor has stayed visible for 10 frames, and
scrolls sideways only when needed.

## The window split (2b.0)

`palang-window.c` had grown to ~1650 lines. It's still one GObject type,
but its code is in four files that share the struct through
`palang-window-private.h`: `palang-window.c` (tabs, files, saving,
session), `-panel.c` (bottom panel, its pages, keys for the terminal),
`-palette.c` (palette, Quick Open) and `-lsp.c` (this phase). Functions
they call in each other start with `_palang_window_`, the usual C hint
for "internal". The key controller that hands keys to the terminal must
still be added *before* the template, so it runs before the app's
shortcuts (`_palang_window_init_key_routing ()`).

## Tests

| Program | Display? | Covers |
|---|---|---|
| `test-lsp` | no | language table; client: handshake and goodbye, no folder, requests, missing program, restarts then giving up, refused initialize, killing a hung server; manager: one server per language, restarts on setting/folder changes, a missing server reported once; positions with an emoji; sync: open/edit/close with UTF-16 ranges, whole-text sync, untitled + Save As, reopen after a crash; diagnostics: placement, severities, gutter marks, messages, updates, clearing, stale versions; definition: Location, LocationLink, nothing, stale answer, no server |
| `test-document` | no | + the `saved` signal |
| `test-settings` | no | + the three server settings |
| `test-window` | xvfb | (servers turned off, so it never starts a real clangd) |

`test-lsp` talks to `tests/fake-lsp-server.c`, a small server built with
the same jsonrpc-glib, whose behaviour the tests choose with options
(`--crash-after-initialized`, `--ignore-shutdown`, `--refuse-initialize`,
`--full-sync`, `--crash-on-change`, `--diagnose`, `--definition L:C`,
`--definition-link`, `--slow-definition`) and which logs every message it
receives to a file the tests read. Its UTF-16 columns come from GLib's
converter, not from Palang's code, so the tests check our conversion
against an independent one.

Real servers, the hover popup, Ctrl+click and quitting with servers
running need the real desktop: see MANUAL_TESTS.md, Phase 2b.
