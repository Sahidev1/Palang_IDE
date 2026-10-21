# Palang IDE — Progress Log

Newest phase at the top. See [PLAN.md](PLAN.md) for the roadmap.

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
