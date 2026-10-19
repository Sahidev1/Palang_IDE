# Palang IDE — Progress Log

Newest phase at the top. See [PLAN.md](PLAN.md) for the roadmap.

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
