# Continuing Palang IDE — handoff for a new session

Read this first, then `PLAN.md` (roadmap and design decisions),
`PROGRESS.md` (what's done), and `docs/01-how-phase-1-works.md` (code map).
Written 2026-10-20 at the end of Phase 1.

## The user

- Call them **sahi**. Pronouns not stated: use they/them.
- Knows **C**, not Rust. **Claude writes nearly all the code; sahi reviews
  it and must be able to understand every line.** Readable plain C over
  clever C, and comments that explain *why* and introduce GTK/GLib patterns
  the first time they appear (see PLAN.md §2b).
- Likes to discuss before coding. When they say "just discuss", don't edit
  code. They review commits and manual test results themselves.

## How we work (agreed conventions)

- **One commit per step** within a phase, with a descriptive message ending
  in `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` (or the
  current model's attribution line from the system prompt).
- **End of each phase:** add an entry at the top of `PROGRESS.md` (what was
  built, verification, bugs found, changes to the plan, open items, where
  to start reading, next), add a phase section to `MANUAL_TESTS.md` for
  things only a person can check, write a `docs/NN-*.md` "how it works"
  guide, and commit them together.
- **`MANUAL_TESTS.md`**: sahi ticks the boxes and writes results under
  each item, then commits. Read their notes and investigate failures; the
  cause isn't always the code (Phase 0: a "failure" was how a terminal was
  started).
- **sudo:** Claude can't run it. Give sahi the exact command; they run it
  in their own terminal.
- Verify before claiming: run the tests, and for UI behaviour drive the real
  app (see "Checking the real app") and look at screenshots.

## Environment

- **Linux Mint 22** (Ubuntu 24.04 base), desktop **MATE**, window manager
  **Marco**, X11 (`DISPLAY=:0`). MATE reports *no* dark-mode preference to
  apps (`color-scheme` = 0), which is why the theme-name fallback exists.
- Libraries: GTK 4.14.5, libadwaita 1.5.0, GtkSourceView 5.12.0, GLib
  2.80, meson 1.3.2. Also installed for later phases: VTE (GTK4), libsoup 3,
  json-glib, jsonrpc-glib, libsecret, debhelper.
- Installed tools: `xvfb` / `xvfb-run`, `xdotool`, `wmctrl`, ImageMagick
  `import`, `gdb`, `valgrind`, `desktop-file-validate`.
- Project path: `/home/ali/Documents/projects/agentic_projects/Palang_IDE`
  (git repo, branch `main`, no remote). App ID: `io.github.sahidev1.Palang`.

## Build, run, test

```sh
meson setup build                 # debug + ASan/UBSan + -Werror by default
ninja -C build
ninja -C build run                # runs with the LSan suppressions
meson test -C build               # 9 programs, 58 cases; UI tests via xvfb-run
tools/build-deb.sh                # .deb of the last *commit* into build-deb/
```

- Running `./build/src/palang` directly prints harmless fontconfig/IBus
  leak reports at exit; use `ninja run` or set
  `LSAN_OPTIONS=suppressions=$PWD/tools/lsan.supp`.
- `tools/lsan.supp` suppresses only: fontconfig, GTK's IBus module, and
  `g_output_stream_write` (a GtkSourceView encoding-conversion leak). Keep
  suppressions narrow; it was verified that our own leaks are still caught
  (deliberately break a `g_clear_object`, see the test fail, restore).
- Tests get isolated `XDG_CONFIG/DATA/STATE/CACHE_HOME` in
  `build/test-home/` (`test_env` in the top `meson.build`). If you run a
  test binary by hand, set those too, or it touches the real config.
- After adding new test programs or tools, `meson setup --reconfigure build`.

## Checking the real app (without touching sahi's settings)

```sh
mkdir -p /tmp/palang-xdg/{config,state}
XDG_CONFIG_HOME=/tmp/palang-xdg/config XDG_STATE_HOME=/tmp/palang-xdg/state \
LSAN_OPTIONS=suppressions=$PWD/tools/lsan.supp ./build/src/palang some/file &
wid=$(xdotool search --name 'Palang$' | head -1)
xdotool windowactivate --sync $wid
xdotool key ctrl+n; xdotool type --delay 40 "hello"   # real input (XTEST)
xdotool mousemove --window $wid X Y click 1          # window-relative
import -window $wid /tmp/shot.png                    # then Read the PNG
xdotool key ctrl+q                                   # quit; check exit code 0
```

- `xdotool key --window …` sends *synthetic* events, which GTK ignores; use
  plain `xdotool key` after `windowactivate`.
- `import -window` doesn't capture popovers (separate X windows); drive
  dropdowns with the keyboard.
- Screenshot only the Palang window: the desktop shows sahi's other apps.
  Delete screenshots afterwards.
- Never modify the repo's real files from outside to test a feature.

## Status

| Phase | State |
|---|---|
| 0 Setup | ✅ |
| 1 Usable editor | ✅ done; manual tests passed except Follow System on MATE (default is Dark now, see PROGRESS.md) |
| 2 Terminal, project search, command palette, LSP | next |
| 3 AI autocomplete · 4 math symbols · 5 math rendering/codegen · 6 Claude Code integration | planned (PLAN.md §5–7) |

Open items: **no license chosen** (`debian/copyright` says TBD), the icon
(`data/icons/…svg`) is a placeholder, Follow System doesn't switch on MATE
(low priority, cause unknown), and the open decisions in PLAN.md §8
(shortcut prefix `_` vs `\`, math rendering engine, default AI provider,
phase order, math index base).

## Code layout (details in docs/01)

```
src/app/        application, window, save flow, session, preferences dialog
src/editor/     PalangDocument (all file I/O), PalangEditor (a tab), search bar
src/workspace/  PalangFileTree + PalangDirectoryModel (our own live listing)
src/settings/   PalangSettings (config.ini, table-driven), palang-appearance
data/ui/        GtkBuilder templates, one per widget; data/palang.gresource.xml
tests/          g_test programs; UI ones listed in tests/meson.build `ui_tests`
```

## Gotchas learned the hard way (don't rediscover these)

1. **C argument evaluation order is unspecified.** Never read `data->x` in
   the same call that does `g_steal_pointer (&data)`. Copy it first.
2. **Don't bind an `AdwBin`'s direct child as a template child**: it gets
   freed twice on dispose. Use `adw_bin_get_child ()`.
3. **Construct properties are applied after `_init()`**: set them in
   `constructed()` (e.g. GtkSourceBuffer `implicit-trailing-newline`).
4. **GtkSourceView:** the loader empties the buffer before it knows the
   file is readable (load into a scratch buffer); a non-UTF-8 byte at the
   end of a file is misdetected as UTF-8 (retry without UTF-8); default
   candidate encodings depend on the locale (set them explicitly).
5. **GTK 4.14 `GtkDirectoryList` misses files created by rename**; we use
   `PalangDirectoryModel`.
6. **`gtk_window_close ()` ignores unrealized windows**, and libadwaita
   swallows close requests while a dialog is closing; after the user
   decides, call `gtk_window_destroy ()` directly.
7. **UI tests:** dialogs only fully close in a *shown* window (use
   `show_window ()`); click their buttons (`answer_dialog` finds the
   button by label) and wait half a second after the dialog appears before
   clicking; animations are disabled in tests. In GTK4 a window's
   `destroy` signal won't fire while the test holds a reference; check
   `gtk_window_get_toplevels ()` instead.
8. **Pango marks weight/style as set even when normal.**
9. **debhelper passes `--buildtype=plain`**; don't add `-Dbuildtype`.
10. **AI/subscription terms:** for autocomplete use API keys (user's own)
    or local models; don't build on claude.ai subscription login in a
    third-party product. Claude Code (the user's own `claude` CLI) is fine
    for agentic features and math translation (PLAN.md §5.4, §6.6, §7).

## Memory

Persistent memory lives in
`~/.claude/projects/-home-ali-Documents-projects-agentic-projects-Palang-IDE/memory/`
(user name, readability requirement, progress-log convention). It loads
automatically in new sessions in this folder.

## Next step

Plan Phase 2 with sahi before coding: integrated
terminal (VTE), project-wide search, command palette, LSP client
(jsonrpc-glib; GNOME Builder's LSP code is the reference). Language-server
types also feed the math type system later (PLAN.md §6.7).
