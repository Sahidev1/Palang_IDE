# Palang IDE — Progress Log

Newest phase at the top. See [PLAN.md](PLAN.md) for the roadmap.

---

## Phase 2c — Hover, completion, Problems page ✅ (2026-10-22)

Resting the pointer on a name shows what its language server says about
it; a completion list opens while typing (with snippets and a Details
pane for documentation); a Problems page lists every error and warning
of the open files. This ends Phase 2 (2a, 2b, 2c).

### What was built (one commit per step)
| Step | Commit | What |
|---|---|---|
| 2b fixes | `774566f` | From sahi's 2b manual tests: palette hover selection (one click per row instead of `single-click-activate`), servers given by full path get their folder on PATH (nvm), first-line toasts; `data/style.css` |
| — | `c4739df` | Stop tracking clangd's `.cache/` |
| 2c.1 | `1c505d9` | Hover: the server's text under the diagnostics; Markdown → Pango markup (`palang-lsp-markdown.c`); shared `send_position_request ()` |
| 2c.2 | `9ff05d2`, `c2edd84` | Completion: `PalangLspCompletionProvider` + `PalangLspCompletionItem`, fuzzy filtering, re-asking incomplete lists, textEdit and extra edits in one undo step; sahi's Enter rule (option C) |
| — | `3fdc9be` | Terminal test prints the screen when its pwd check fails |
| 2c.3 | `75216fa` | Snippets (own LSP snippet parser), Details pane with `completionItem/resolve` |
| 2c.4 | `37b2e22` | Problems page (`PalangLspProblem`, `get_problems ()`, `PalangLspProblemsPanel`, Ctrl+Shift+M) |
| 2c.5 | this commit | End of phase, plus: exact-case entries first, "Show Completions" (Ctrl+Space), one-line problem messages |

### Verification
- **Tests:** 14 test programs, 136 test cases, plus the desktop-file check
  (Phase 2b: 116 cases). `test-lsp` now runs under xvfb-run (it creates
  editor views) and has 42 cases: Markdown samples shaped like the three
  servers' real answers, snippet parsing, completion items (fields,
  matching and order, insertion with undo, snippets), hover in every
  answer form, completion requests and resolve, the problems list and
  panel. The fake server learned `--hover KIND`, `--complete`, `--slow`.
  The LSP tests passed 5 repeated runs after 2c.1; the full suite 20 of
  21 runs after 2c.2 (the one failure: the terminal test, see open items).
- **Real app** on Xvfb with clangd, pylsp and typescript-language-server
  (screenshots): hover text formatted for all three; completion after
  `.` (struct fields), while typing (`printf` with clangd re-asking 3
  times for 6 letters), `xs.ma` → `map` first; the Enter rule; the
  `printf(…)` snippet with Tab; "mit" + Tab stays a tab; the Details pane
  with typescript's documentation; `PRIX8` adding `#include` with one
  undo; the Problems page (sorting, click → right line and column, list
  following edits).
- **valgrind** (release build, clangd, completion with a snippet, Problems
  page): no invalid reads or writes; 29 definite leaks: fontconfig (26,
  two through Pango's font measuring for `tab-width`), GIO's module
  loading (1), GLib's load-time setup (1), and 32 bytes inside GtkSourceView's own
  gutter drawing (a list it never hands out); "conditional jump" reports
  only in generated code (Mesa) and librsvg.

### Bugs found and fixed on the way
- **Palette: Down at the bottom jumped back** with the pointer on the list
  (sahi's 2b manual test): `single-click-activate` also selects the row
  under the pointer.
- **A use-after-free from 2b in `add_diagnostic ()`**, found by ASan once
  2c.4 copied the message: `g_variant_lookup ()` turns a GVariant built in
  memory into bytes and frees the old pieces, so earlier string pointers
  dangle. Only tests build such variants; real server messages arrive as
  bytes. CLAUDE.md gotcha 19.
- **Clicking a problem went one line too high** (0- vs 1-based lines).
- **Completion rows kept the bold letters of what was typed before**: rows
  that stay in the list aren't redrawn; `rescore ()` now re-adds the items.
- **GtkSourceView traps** (CLAUDE.md gotchas 16–18): its completion takes
  Enter/Up/Down first (our key controller sits on the view's parent);
  snippets move on with Tab only with `enable-snippets`, which also turns
  on its license snippets (library emptied); its Details cell doesn't wrap
  and an empty or too-wide pane isn't shown.
- **typescript's details made the popup as wide as the window**: cut at 40
  characters after the label (the Details pane shows all).
- **`PRIX8` gave `PRIx8`**: the matcher's score favours lowercase; entries
  starting with exactly what was typed now come first.
- **Problem messages showed ⏎ symbols** in single-line mode; line breaks
  are now spaces.

### Changes to the plan
- **2c steps decided with sahi** (docs/plan/phase-2.md): hover, completion
  core, snippets + Details, Problems page, end of phase. "Go back"
  (Alt+Left) left open.
- **Enter rule** (sahi, option C): Enter after a word that already is the
  first entry starts a new line; Tab still takes the entry.
- **JavaScript server from the desktop menu** (2b manual test): option A,
  a full path in `javascript-server`; the toast's Details say how.
- **Ctrl+Q with a dialog open** closes the dialog (libadwaita): kept, as
  sahi decided.
- **Fuzzy matching in `lsp/`** uses GtkSourceView's matcher, not the
  palette's (dependency rule).
- **Completion answers that arrive after more typing are kept** (filtered),
  unlike definition and hover answers.
- **Added beyond the plan:** "Show Completions" (Ctrl+Space; GtkSourceView 5
  has no key for it).

### Open items
- **Ctrl+Space after the list was closed** (by Escape or moving the
  cursor) shows nothing on the virtual display the first time; a second
  request (key or palette) shows it. The action runs, the server answers
  and GtkSourceView gets 86 entries, but doesn't show its popup; reading
  its code didn't find why. Manual test asks sahi to check the desktop.
- **The terminal test failed once in 21 runs** (`pwd` output not found);
  it now prints the screen when that happens.
- **"Go back"** (Alt+Left after a jump): not done.
- **Problems list covers open files only** (servers report on those).
- **clangd adds `<cinttypes>` (the C++ name) in a `.c` file** when
  completing `PRIX8`; Palang sends the language "c"; clangd's choice.
- From earlier: gutter icons are grey, license, icon, Follow System on
  MATE; the hover and completion popups draw a black box on Xvfb (no
  compositor), fine on sahi's desktop for hover.

### Where to start reading
1. `docs/04-how-phase-2c-works.md`.
2. `src/lsp/palang-lsp-completion-provider.c` (top comment), then
   `palang-lsp-completion-item.c`.

### Next
sahi runs MANUAL_TESTS.md, Phase 2c. Then Phase 3 (AI autocomplete) or
Phase 4 (math symbols): the order is open decision 5 (PLAN.md §5).

---

## Phase 2b — Language servers: sync, diagnostics, go to definition ✅ (2026-10-21)

Palang now starts a language server for C/C++ (clangd), Python (pylsp)
and JavaScript/TypeScript (typescript-language-server), keeps its copy of
every open file up to date, shows its errors and warnings in the editor,
and jumps to definitions (F12, Ctrl+click). Hover information and
completion are Phase 2c.

### What was built (one commit per step)
| Step | Commit | What |
|---|---|---|
| plan | `102b9b2` | Decisions: 2b/2c split, C + Python (pylsp) + JavaScript, no Rust, Problems page later |
| 2b.0 | `b1a3083` | `palang-window.c` split into `-panel.c` and `-palette.c` (shared struct in `palang-window-private.h`); no behaviour change |
| 2b.1 | `fc238d0` | Client core: `PalangLspClient` (one server process: handshake, shutdown, restart after crashes), `PalangLspManager` (one server per language per window), `[lsp]` settings, failure toasts with Details, "Restart Language Servers" |
| 2b.2 | `af2eb97` | Document sync: `PalangLspDocument` (didOpen/didChange/didSave/didClose, versions), UTF-16 positions, `PalangDocument::saved` |
| 2b.3 | `b2012fd` | Diagnostics: wavy underlines, gutter icons, message on hover (`PalangLspHoverProvider`) |
| 2b.4 | `37a4beb` | Go to definition: F12 and Ctrl+click |

### Verification
- **Tests:** 14 test programs, 116 test cases, plus the desktop-file check
  (Phase 2a: 13 programs, 92 cases). New: `test-lsp` (24 cases) against
  `tests/fake-lsp-server.c`, a small JSON-RPC server that tests tell how
  to behave (crash, hang, refuse to start, publish diagnostics, answer
  definitions, answer late). All pass under ASan/UBSan; `test-lsp` passed
  10 repeated runs after each step.
- **Real app** on Xvfb, with the real servers:
  - clangd and pylsp start when a file of their language opens, and
    quitting stops them (exit code 0, no servers left over).
  - Typing a line with an emoji into a C file: clangd's log shows the
    edit positions jump by two across the emoji, and its only diagnostic
    lands on the right column.
  - Diagnostics from all three servers appear at the right places; hovering
    an error shows its message.
  - F12 on `palang_application_new ()` in `main.c` opens its definition;
    Ctrl+click on `g_application_run ()` opens GLib's `gapplication.h` at
    line 224.
  - A missing server, a server that refuses to start, and "Restart
    Language Servers" all work as described below.
- **valgrind** (build without sanitizers, with clangd and pylsp, F12 and
  hover): no invalid reads or writes; the definite leaks are all in
  fontconfig (13), GIO's module loading (2) and GLib's load-time setup
  (1); "uninitialised value" reports are only in librsvg and Mesa's
  generated code.

### Bugs found and fixed on the way
- **An error reply to `initialize` was treated as a crash** (retried three
  times, then "keeps stopping"), hiding the server's own explanation. It
  now fails at once with the server's message.
- **The 2-second kill timeout could fire up to a second early**
  (`g_timeout_add_seconds` rounds to whole-second ticks); found by
  repeated test runs.
- **Toasts cut long messages** to one line: failure toasts got a Details
  button with the full, selectable text.
- **Hover missed one-letter errors**: GtkSourceHover gives the character
  boundary *nearest* the pointer, so over the right half of `x` it was
  past the range. A range's end now counts as inside.
- **Go to line in a new tab could stop scrolling too early** (a Phase 2a
  bug, found by go to definition into a long file): a new text view's
  line positions are estimates for a few frames. It now waits until the
  cursor stays visible, and no longer scrolls sideways needlessly.
- **The window tests started the real clangd** (they open `a.c`); they now
  turn the servers off.

### Changes to the plan
- **Phase 2 split into 2b and 2c** (decided before coding); hover
  information and completion move to 2c, together with the Problems page.
- **Setup found on this machine** (now in the install hints and the plan):
  pylsp reports nothing without `python3-pyflakes`; typescript-language-
  server 6 needs TypeScript 5 (7 has no `tsserver`), installed under the
  same nvm Node version as the server.
- **Untitled documents** reach the server only once saved (servers need a
  file name).
- **Columns of a definition in a file that isn't open** are the server's
  UTF-16 count, used as characters (off only after emoji and similar on
  that line); exact when the file is open.
- **Gutter icons have no tooltips** (GtkSourceView 5 dropped them); the
  message shows on hovering the underlined text.

### Open items
- Gutter icons are grey (symbolic icons take the gutter's text color);
  shape tells error from warning. Coloured icons would need our own
  paintables.
- No "go back" after a jump (Alt+Left in other editors).
- The hover popup draws a black box around itself on Xvfb (no
  compositor); check on the real desktop (manual tests).
- Still open from earlier: license, icon, Follow System on MATE.

### Where to start reading
1. `docs/03-how-phase-2b-works.md`.
2. `src/lsp/palang-lsp-client.c` (the server's life), then
   `palang-lsp-document.c` (sync) and `palang-lsp-manager.c`.

### Next
Phase 2c: hover information and completion, maybe the Problems page.

---

## Phase 2a — Terminal, `palang` command, search, palette ✅ (2026-10-21)

Palang now has a bottom panel with a terminal and project-wide search, a
`palang` command to change settings from that terminal, a command palette
(Ctrl+Shift+P) and Quick Open (Ctrl+P).

### What was built (one commit per step)
| Step | Commit | What |
|---|---|---|
| 2a.1 | `d3cbe71` | Bottom panel (Ctrl+J, height and page in the session), `active-editor` property, the command table |
| 2a.2 | `b0679c8` | Terminal (VTE): shell in the project folder, restart on Enter, Ctrl+Shift+C/V, follows style and font; keys go to the shell |
| 2a.3 | `397b8f1` | `palang help/settings/get/set/reset` in the Palang terminal, settings by name in `settings/`, bash completion |
| 2a.4 | `5128b0f` | Project search with ripgrep (Ctrl+Shift+F), open a result at its line |
| 2a.5 | `19b0756` | Command palette and Quick Open, fuzzy matching |

### Verification
- **Tests:** 13 test programs, 91 test cases, plus the desktop-file check
  (Phase 1: 9 programs, 58 cases). New: command-line, search, fuzzy,
  terminal, palette. All pass under ASan/UBSan, repeatedly, and again in
  the optimized, hardened Debian build (`tools/build-deb.sh`).
- **Real app**, driven with xdotool (on Xvfb when the clipboard or
  settings were involved, so your desktop wasn't touched), checked with
  screenshots: panel toggle and height across restarts; terminal prompt in
  the folder, `PALANG_TERMINAL`, `which palang` → `build/src/palang`,
  Ctrl+W deleting a word, exit and restart, copy and paste, light style and
  font; `palang get/set` with errors and exit status, a live switch to
  light; search with grouped results, an invalid regex message, opening a
  result at its line; the palette running Toggle Panel; Quick Open opening
  a file. Every run exited with code 0 and no sanitizer reports.
- **valgrind** (build without sanitizers, the same steps): no invalid
  reads or writes; the 17 "definitely lost" records are all allocated
  inside fontconfig (16) and GIO's module loading at startup (1).
- **Package:** builds; depends on VTE, json-glib and ripgrep; installs the
  bash completion.

### Bugs found and fixed on the way
- **Ctrl+W in the terminal closed an editor tab.** GTK4 runs the app's
  shortcuts before the focused widget. A window key controller now hands
  keys to the terminal first; only a few commands keep their shortcuts
  there (see docs/02).
- **Use-after-free when closing** (ASan): VTE reports the shell's exit
  while being destroyed. The terminal disconnects first.
- **`palang set …` opened files instead:** GApplication sends the caller's
  environment only with `G_APPLICATION_SEND_ENVIRONMENT`.
- **Go to line in a new tab didn't scroll** (a Phase 1 bug, found by the
  search's "open at line"): `palang_editor_go_to ()` now keeps scrolling
  on each frame until the cursor is visible.
- **The panel lost a pixel per hide/show,** and opened at its minimum
  height: its height is now measured from the divider.
- **Fuzzy matching missed better matches** ("pan" → the "pa" of
  "palang-"): it now tries every start.

### Changes to the plan
- **Search results show the file path above each file's first match**
  instead of GTK list section headers: in GTK 4.14 the first header stayed
  scrolled out of view.
- **rg runs with `--sort path`** (one thread) so results and the 5000 cut
  are the same every time.
- **Keys in the terminal** (not in the plan): the shell gets them all
  except Ctrl+J, F9, Ctrl+Shift+F, Ctrl+Shift+P, **Ctrl+P**, Ctrl+, and
  Ctrl+Q. Ctrl+P is bash's "previous command" (Up does the same); VS Code
  takes it for Quick Open too. Easy to change: `commands_kept_in_terminal[]`.
- **New public API used by later phases:** `palang_window_open_file_at ()`
  (LSP go-to-definition), `PalangPanel` pages (a diagnostics list), the
  reusable `PalangPalette` (math symbols, Phase 4).
- **lsan.supp:** VTE 0.76 leaks a small block on every copy/selection
  (matched on `gdk_content_formats_builder_new`).

### Manual test results (sahi, 2026-10-21)
All Phase 2a manual tests passed. Two notes:

- **Dialogs stacked:** pressing Ctrl+Shift+P, Ctrl+P or Ctrl+, again opened
  another dialog on top. Fixed after the tests: one palette at a time (the
  other palette's shortcut switches to it), one Preferences. Its test also
  found a crash when Quick Open lists a folder without files (fixed).
- **Tab with width 2 moved one space after a letter:** expected; Tab goes
  to the next tab stop (see MANUAL_TESTS.md, results notes).

### Open items
- **`palang-window.c` is ~1600 lines.** Move the panel pages, key routing
  and palette actions into their own file before 2b adds LSP wiring
  (ARCHITECTURE.md rule 4).
- Bash completion only loads from the installed package (in development:
  `source data/palang.bash-completion`).
- Still open from Phase 1: license, icon, Follow System on MATE.

### Where to start reading
1. `docs/02-how-phase-2a-works.md`.
2. `src/search/palang-search.c` (the async subprocess pattern), then the
   "Bottom panel" and terminal sections of `src/app/palang-window.c`.

### Next
Phase 2b: the LSP client (needs `sudo apt install clangd` first).

---

## Phase 1 — A usable editor ✅ (2026-10-20)

Palang is now a working editor: tabs, a folder tree, find and replace,
preferences, light/dark on MATE, following outside changes, and session
restore. It installs as a 36 KB `.deb`.

### What was built (one commit per step)
| Step | Commit | What |
|---|---|---|
| 1.1 | `86e31e3` | `PalangDocument`: async load/save keeping encoding and line endings, Save As, overwrite protection, language detection |
| 1.2 | `88c9689` | Tabs, New/Open/Save/Save As/Save All/Close, unsaved-changes prompts, files from the command line |
| 1.3 | `7010542` | Folder tree sidebar (our own `PalangDirectoryModel`), Open Folder |
| 1.4 | `065336c` | Find/Replace bar (case, whole word, regex), Go to Line |
| 1.5 | `f0af6ad` | Settings in `config.ini`, Preferences window, Style setting with the MATE theme-name fallback |
| 1.6 | `b644470` | Reload or banner when other programs change files; session restore |
| 1.7 | `ad12234`, `8f3366f` | Desktop entry, icon, Debian packaging, `tools/build-deb.sh` |

### Verification
- **Tests:** 9 test programs, 58 test cases plus a desktop-file check: document, settings,
  session, window, file tree, editor, appearance, application, desktop
  file. All pass under ASan/UBSan, repeatedly (no flaky runs in the final
  20+ runs), and again in the optimized, hardened Debian build.
- **Real app:** driven with xdotool and checked with screenshots: tabs,
  close prompts (Cancel, Discard, Discard All), file tree expand and open,
  Find with F3, Go to Line, Preferences switching to Dark, session restore.
  Every run exited with code 0 and no memory errors.
- **Manual tests** for you: `MANUAL_TESTS.md`, Phase 1 section (system
  dialogs, installing the package, your real MATE theme, big files).

### Bugs found and fixed on the way
- **Crash:** clicking Save in the close-tab prompt read a pointer another
  argument had already cleared (C argument order is unspecified). UBSan.
- **Window wouldn't close** after "Save All" in the quit prompt
  (libadwaita swallowed the second close request). Now destroyed directly.
- **Double free** when an `AdwBin`'s direct child was bound as a template
  child. Twice; both fixed, rule written down in `docs/01`.
- **Leak in a helper** (`g_strstrip (g_strdup (…))`) caught in review
  before it shipped.

### Changes to the plan
- **Our own directory model** instead of GTK's `GtkDirectoryList`, which
  misses files created by rename (how many editors save, and `mv`).
- **GtkSourceView workarounds** in `PalangDocument`: load via a scratch
  buffer (the loader empties the buffer before failing), no implicit
  trailing newline, retry without UTF-8 for a misdetected Latin-1 ending.
- **Leak suppressions** (`tools/lsan.supp`): added IBus and one small
  GtkSourceView encoding-conversion leak, both matched narrowly. A test
  confirmed our own leaks are still caught.
- **UI tests run under `xvfb-run`**, in their own config/state folders
  (`build/test-home/`). Dialogs are tested by clicking their buttons.
- **Default indentation is 4 spaces** (Preferences can change it).

### Manual test results (sahi, 2026-10-20)
All Phase 1 manual tests passed except one (details in `MANUAL_TESTS.md`):

- **Failed: "Follow System" doesn't follow MATE.** Switching MATE to
  Mint-Y-Dark (or a similar dark theme) left Palang light. Checked
  afterwards: GTK4 does receive MATE's theme name live (it reported
  `Windows XP Luna`, the same as `org.mate.interface gtk-theme`), and the
  name check in `palang_appearance_choose_color_scheme()` is unit-tested,
  so the cause is still unknown. Even when found, guessing from the name
  misses dark themes without "dark" in it (`Redmond97 Matrix`, …).
  **Decision:** low priority for now; the **default style is Dark**
  instead of Follow System (`palang-settings.c`). Follow System is still
  in Preferences. Possible future fix: find the cause, then decide dark
  from the theme's actual background colour instead of its name.
- **Fixed: Backspace after Tab** removed one space instead of the whole
  indent. GtkSourceView's `smart-backspace` is now on
  (`data/ui/palang-editor.ui`); checked in the real app.

### Open items
- **License:** none chosen yet; `debian/copyright` says "TBD (all rights
  reserved)". Needed before sharing the package.
- **Icon** is a placeholder.
- **Follow System on MATE** doesn't work (see above).

### Where to start reading
1. `docs/01-how-phase-1-works.md`: map of the code and the reasons behind
   the non-obvious decisions.
2. `src/editor/palang-document.c`, then `src/app/palang-window.c`.

### Next
Phase 2: integrated terminal, project-wide search, command palette, LSP.

---

## Phase 0 — Project setup ✅ (2026-10-20)

Palang builds, opens a window and exits cleanly, with the memory checkers on.

### What was built
- **Build system:** `meson.build`, `data/meson.build`, `src/meson.build` and
  `tests/meson.build`. Debug builds use `-Wall -Wextra -Werror` plus
  AddressSanitizer and UndefinedBehaviorSanitizer by default.
- **Application:** `src/main.c`, `src/app/palang-application.c/.h`. Sets up the
  app, the `app.quit` action and the Ctrl+Q shortcut.
- **Window:** `src/app/palang-window.c/.h` + `data/ui/palang-window.ui`. A
  libadwaita header bar and a GtkSourceView with C syntax highlighting, line
  numbers and current-line highlight. Colours follow the system light/dark
  setting.
- **Test:** `tests/test-application.c` (checks the application ID).
- **Docs:** `docs/00-how-gtk-apps-work.md`, a primer on GObject, reference
  counting, signals, templates, the main loop and async code, using Palang's
  own files as examples. `README.md` has build instructions.
- **Application ID:** `io.github.sahidev1.Palang`.

### Verification
- **Build:** no warnings (warnings are errors).
- **Tests:** `meson test -C build` passes under the sanitizers.
- **App:** the window renders as expected. Quitting through `app.quit` (the
  same action as Ctrl+Q) exits with code 0 and no memory errors.

### Addition to the plan: leak suppressions
On the first run, LeakSanitizer reported 405 leaks. All were allocated inside
**fontconfig** (the system font library), which caches font data until the
process exits. None came from Palang's code. `tools/lsan.supp` now ignores
fontconfig only, so real leaks in our code are still reported.

`ninja -C build run` and `meson test` load the suppression file
automatically. Running `./build/src/palang` directly also works, but prints the
harmless fontconfig reports at exit.

### Where to start reading
1. `docs/00-how-gtk-apps-work.md`
2. `src/app/palang-application.c`, then `src/app/palang-window.c` with
   `data/ui/palang-window.ui` alongside it.

### Next
Phase 1: file tree, tabs, opening and saving files.
