# How Phase 2c works

A guide to the code added in Phase 2c: hover information, completion
(with snippets and a documentation pane), and the Problems page, all in
module `lsp/`. It explains which file does what, how the pieces connect,
and the non-obvious decisions with their reasons. Phase 2b's guide
(`03-how-phase-2b-works.md`) covers the client, document sync and
diagnostics this builds on.

## The big picture

```
PalangLspManager                          src/lsp/palang-lsp-manager.c
 ├ send_position_request ()   shared by definition, hover, completion
 ├ hover_async ()             → Markdown text
 ├ complete_async ()          → list of PalangLspCompletionItem
 ├ resolve_completion_async () → the rest of one item (documentation)
 ├ get_problems ()            → live list of PalangLspProblem
 └ add_view (view)            sets up each editor view:
     ├ PalangLspHoverProvider       src/lsp/palang-lsp-hover-provider.c
     └ PalangLspCompletionProvider  src/lsp/palang-lsp-completion-provider.c
         └ PalangLspCompletionItem  src/lsp/palang-lsp-completion-item.c
             └ palang-lsp-snippet.c   LSP snippet syntax → parts

palang-lsp-markdown.c        server Markdown → Pango markup (hover, Details)
palang-lsp-problem.c         one diagnostic as a value
palang-lsp-problems-panel.c  the Problems page (template in data/ui/)

palang-window-lsp.c (app/)   adds the Problems page, Ctrl+Shift+M,
                             problem clicked → palang_window_open_file_at ()
```

Everything new except the window wiring is in `lsp/`. The providers are
GtkSourceView's extension points (`GtkSourceHoverProvider`,
`GtkSourceCompletionProvider`): we implement their interfaces and
GtkSourceView calls us.

## Requests about a position

Go to definition, hover and completion all ask the server about "the
position here" in a document. `send_position_request ()` does the common
part:

1. Find the document's PalangLspDocument and its server; fail with
   `G_IO_ERROR_NOT_CONNECTED` if it isn't open on one.
2. Check that the server announced the feature in its reply to
   `initialize` (`definitionProvider`, `hoverProvider`,
   `completionProvider`); fail with `G_IO_ERROR_NOT_SUPPORTED` if not.
   (A server that doesn't do hover would otherwise answer every hover
   with an error.)
3. Remember the document's version, convert the position to UTF-16
   (`palang-lsp-position.h`), add any extra parameters (completion sends
   a "context": why we ask, and the trigger character), and send.

When the answer comes, definition and hover call `return_if_stale ()`:
if the text changed meanwhile, the answer is about old text and is
dropped. **Completion keeps it**: people type on while the server
thinks, and dropping every late answer would leave the popup empty. The
list is filtered by what was typed since instead (see below).

## Hover

`PalangLspHoverProvider` implements the *async* pair
`populate_async ()`/`populate_finish ()`. It adds the diagnostic
messages under the pointer at once, then asks the server; when the answer
comes it adds a separator and the server's text. GtkSourceView shows the
popup once all providers are done, and cancels us if the pointer moves
on (`g_task_return_error_if_cancelled ()`: we add nothing to a popup
that's gone).

The answer can come in three shapes (MarkupContent, the older
MarkedString, or an array of them); `hover_contents_to_markdown ()` in
the manager turns all of them into one Markdown string. Long text goes
into a GtkScrolledWindow that grows with its content up to 400 pixels
(`propagate-natural-height` and `max-content-height`).

## Markdown → markup

Servers write Markdown; GtkLabel shows *Pango markup* (`<b>`, `<i>`,
`<tt>`). `palang_lsp_markdown_to_markup ()` converts the bits the three
servers actually use (we captured their real answers first): code blocks
and `inline code` in monospace, `###` headings bold, `---` as an empty
line, `*italic*`/`**bold**`, and backslash escapes (pylsp writes
`\*\*width\*\*`). Everything else stays as written.

It works line by line, and every piece of server text goes through
`g_markup_escape_text ()`, with every tag closed on the same line, so the
result is always valid markup: a `<` in a C++ type or an unclosed `*`
just shows as written. A server that answers in plain text is first
escaped as Markdown (`palang_lsp_markdown_escape ()`), so its text shows
exactly.

## Completion

### How GtkSourceView drives a provider

The comment at the top of `palang-lsp-completion-provider.c` lists the
calls: `is_trigger ()` (should this character open the popup? we answer
from the server's `triggerCharacters`), `populate_async ()` (a new
completion: ask the server), `refilter ()` (the typed word changed),
`display ()` (fill one row), `activate ()` (insert the chosen entry).

### The list: a chain of list models

The answer goes into a `GListStore`, wrapped in a `GtkFilterListModel`
(only items matching what's typed) wrapped in a `GtkSortListModel` (best
match first, then the server's `sortText`). That last model is what
GtkSourceView shows. When the typed word changes, `rescore ()` matches
every item again and puts the same items back into the store: that
"items-changed" makes the filter and sorter look again *and* makes the
popup redraw rows, so the bold letters match what's typed now.

Matching uses GtkSourceView's fuzzy matcher
(`gtk_source_completion_fuzzy_match ()`: lower score is better, case is
ignored), not the palette's: ARCHITECTURE.md rule 2 keeps `lsp/` from
including `palette/`'s headers. Its score favours lowercase ("PRIx8"
scores better than "PRIX8" for "PRIX8"), so `compare ()` first puts the
entries that start with exactly what was typed, case included; then the
score; then the server's `sortText`.

Ctrl+Space ("Show Completions", `palang-window-lsp.c`) opens the list
without typing: GtkSourceView 5 has no key for it (version 4 had one).

clangd sends at most 100 entries and says the list is *incomplete*. Then
each `refilter ()` asks again for the longer word (cancelling the
previous question) and replaces the store's items when the answer comes.

The data behind one model (`Results`: the store, what was typed, the
incomplete flag) is attached to the model with `g_object_set_data_full
()`, so `refilter ()`, which only gets the model, finds it again, and it
is freed with the model.

### One entry, and inserting it

`PalangLspCompletionItem` reads one CompletionItem: the label (without
clangd's leading space), filter text, sort text, detail, kind, the text
to insert (`textEdit.newText`, else `insertText`, else the label), and
extra edits (`additionalTextEdits`, like clangd's `#include`).

`palang_lsp_completion_item_insert ()` replaces the typed word: from
where the server's `textEdit` starts if it gave one, to the cursor. The
extra edits are usually above the word (at the `#include`s), so the word
is first held in two GtkTextMarks: marks move along when text before
them changes, iters don't. All of it is one user action, so one Ctrl+Z
undoes it.

### The Enter rule

The popup opens with its first entry selected (`select-on-show`), so
Enter or Tab takes it. After typing a whole word like `return`, that
entry is often the same word, and Enter should start a new line (sahi's
choice, option C). So when the word before the cursor already is the
first entry's filter text, and Up/Down weren't pressed, Enter closes the
popup and goes on to the editor. Tab still takes the entry.

Two GtkSourceView details made this harder (CLAUDE.md gotcha 16): its
completion takes Enter, Up and Down with its own capture-phase key
controller on the view, which runs before any added to the view later;
and it doesn't tell when its popup opens. So the provider's key
controller sits on the view's *parent* (in the capture phase an event
travels from the window down, so the parent sees it first), and the
provider remembers the list of its last completion itself.

### Snippets

With `snippetSupport` announced, servers send entries like
`printf(${1:const char *format, ...})`: places to fill in, Tab moving
between them. GtkSourceView can insert snippets, but its snippet *parser*
reads its own syntax (a bare `$1` means something else there). So
`palang-lsp-snippet.c` parses LSP's syntax into parts (plain text, or a
tab stop with its default text), and the item builds the
`GtkSourceSnippet` chunk by chunk, marking each chunk's text as final
("text-set") so GtkSourceView doesn't expand it again. Without a `$0` an
empty one is added at the end, so the last Tab leaves the snippet.

Tab only moves through a snippet when the view's `enable-snippets` is on
(CLAUDE.md gotcha 17). That also expands GtkSourceView's own snippet
files (here only license texts: "mit" + Tab), so `add_view ()` empties the
snippet manager's search path. That is application-wide; Phase 4 may want
to change it for its own snippets.

### The Details pane

The popup's **Details** button shows the selected entry's DETAILS cell:
we fill it with the detail (a full signature, in monospace) and the
documentation (Markdown → markup). clangd sends documentation with the
list; pylsp and typescript-language-server only on request
(`completionItem/resolve`, with the entry exactly as it came). So
`show_details ()` asks when an entry is shown there, shows "Loading…"
meanwhile, and fills the cell when the answer comes, if the cell still
shows that entry.

Two GtkSourceView details (CLAUDE.md gotcha 18): its cell's own label
doesn't wrap, and a pane too wide to place isn't shown at all, so we put
our own wrapped label in it; and an empty cell keeps the pane closed,
hence "Loading…". Details after a label in the list are cut at 40
characters (typescript's are whole signatures, which made the popup as
wide as the window).

## The Problems page

Each PalangLspDocument now emits `diagnostics-changed` when its
diagnostics are replaced or cleared (new ones from the server, the file
closed, the server restarted). The manager listens to every document and
rebuilds `get_problems ()`: a GListStore of PalangLspProblem (file,
line, column, severity, message, source; positions as the diagnostics'
marks are then), sorted by file and position. A few hundred entries at
most, so rebuilding is simpler than patching; `g_list_store_splice ()`
replaces the list in one change.

The page (`PalangLspProblemsPanel`) shows that list: one row per problem,
with a click gesture per row like the command palette (not
`single-click-activate`, which selects the row under the pointer and
fights the keyboard). It emits `problem-activated`; the window opens the
file there. Problems count lines and columns from 0, the window from 1:
the handler adds 1 (that was a bug on the first try).

Only open files are listed: servers report on the files they have open.

## A GVariant trap found on the way

`add_diagnostic ()` read the message as a pointer into the diagnostic's
GVariant, then called `g_variant_lookup ()` for the severity. For a
GVariant built in memory, the first `g_variant_get_data ()` (which the
lookup calls) converts it to bytes in place and frees the old pieces, so
the pointer dangled. Messages from servers arrive as bytes, so only a
test hit it (ASan). The fix is to call `g_variant_get_data ()` before
taking pointers. CLAUDE.md gotcha 19.

## Tests

`tests/test-lsp.c` (now run under xvfb-run, since it creates editor
views) with the fake server's new options (list at the top of
`tests/fake-lsp-server.c`):

- `--hover KIND`: answers hover as markdown, plaintext, the older array
  form, or nothing.
- `--complete`: an incomplete completion list with a textEdit, an extra
  edit, and answers to `completionItem/resolve`.
- `--slow` delays definition and hover, for the stale-answer tests.

Pure parts have their own tests: Markdown (samples shaped like the real
servers' answers, and edge cases), snippets (every syntax form and
broken input), completion items (fields, matching and order, insertion
with undo, snippets with the selected placeholder). The providers'
GtkSourceView side can't be driven from tests (its contexts can't be
created there); it was checked in the real app and is in MANUAL_TESTS.md.

## Where to start reading

1. `palang-lsp-completion-provider.c`, top comment, then
   `palang-lsp-completion-item.c` (`insert ()`).
2. `palang-lsp-markdown.c` and `palang-lsp-snippet.c`: small, pure.
3. `send_position_request ()` in `palang-lsp-manager.c`.
