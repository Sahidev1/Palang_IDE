# Palang IDE — Progress Log

Newest phase at the top. See [PLAN.md](PLAN.md) for the roadmap.

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
