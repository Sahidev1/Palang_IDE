# Continuing Palang IDE — handoff for a new session

Read this first, then, in order:
1. `ARCHITECTURE.md`: modules, dependency rules, invariants, conventions.
2. `docs/interfaces.md`: what each existing module offers (points to the
   headers).
3. `PLAN.md` (short roadmap) and **only the current phase's** design in
   `docs/plan/`.
4. The newest entry of `PROGRESS.md`.

Open implementation files (and the `docs/NN-*.md` guides) only for the
modules you're changing. Everything read stays in context for the whole
session, so read what the task needs (line ranges, not whole files, when
that's enough).

Written 2026-10-20 at the end of Phase 1; reading order added the same day.

## The user

- Call them **sahi**. Pronouns not stated: use they/them.
- Knows **C**, not Rust. **Claude writes nearly all the code; sahi reviews
  it and must be able to understand every line.** Readable plain C over
  clever C, and comments that explain *why* and introduce GTK/GLib patterns
  the first time they appear (see ARCHITECTURE.md §4).
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
  guide, **update `docs/interfaces.md`**, and commit them together.
- **One Claude session per phase** (Phase 2 is split: 2a, 2b). See the
  "Sessions" column in PLAN.md's roadmap.
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
  `import`, `gdb`, `valgrind`, `desktop-file-validate`, `ripgrep` (`rg`).
- Project path: `/home/ali/Documents/projects/agentic_projects/Palang_IDE`
  (git repo, branch `main`, no remote). App ID: `io.github.sahidev1.Palang`.

## Build, run, test

```sh
meson setup build                 # debug + ASan/UBSan + -Werror by default
ninja -C build
ninja -C build run                # runs with the LSan suppressions
meson test -C build               # 14 programs, 136 cases; UI tests via xvfb-run
tools/build-deb.sh                # .deb of the last *commit* into build-deb/
```

- Running `./build/src/palang` directly prints harmless fontconfig/IBus
  leak reports at exit; use `ninja run` or set
  `LSAN_OPTIONS=suppressions=$PWD/tools/lsan.supp`.
- `tools/lsan.supp` suppresses only: fontconfig, GTK's IBus module,
  `g_output_stream_write` (a GtkSourceView encoding-conversion leak) and
  `gdk_content_formats_builder_new` (VTE leaks one per copy/selection). Keep
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
- For anything touching the clipboard or many keys, run the whole session
  on a separate virtual display instead: `xvfb-run -a -s "-screen 0
  1100x800x24" bash script.sh`, with `xdotool search --onlyvisible --name
  Palang` (there is no window manager there, so skip `windowactivate`).
- Never modify the repo's real files from outside to test a feature.
- **First check `pgrep -a palang`.** Palang is a single-instance
  GApplication on the D-Bus session bus, and `xvfb-run` shares that bus:
  if sahi has Palang open, the files go to *their* window instead
  (happened 2026-10-22). Ask them to close it, or run under
  `dbus-run-session --` (the window then takes ~25 s to appear while the
  portals time out).
- **On sahi's real display, touch only Palang's own windows**, found by its
  PID (`xdotool search --pid $pid`), never by a name pattern: a cleanup
  loop over windows named "Save" once closed the screensaver's windows and
  another app's dialog (2026-10-22). No `windowclose`/`windowkill` on
  anything else, and no screenshots of other apps.

## Status

| Phase | State |
|---|---|
| 0 Setup | ✅ |
| 1 Usable editor | ✅ done; manual tests passed except Follow System on MATE (default is Dark now, see PROGRESS.md) |
| 2a Terminal, `palang` command, project search, palette + Quick Open | ✅ done; manual tests passed (dialog stacking fixed afterwards) |
| 2b LSP client (core, sync, diagnostics, go to definition) | ✅ done; manual tests passed (JavaScript from the menu: full path, see PROGRESS.md 2c) |
| 2c LSP hover, completion (snippets, Details pane), Problems page | ✅ done; manual tests (MANUAL_TESTS.md, Phase 2c) not yet run by sahi |
| Interim: look and feel (logo, icons, amber, contrast), tree-hover and Save As fixes | ✅ done; manual tests (MANUAL_TESTS.md, Interim) not yet run by sahi |
| **3 AI autocomplete (ghost text)** | **next** (decided 2026-10-22) |
| 4 math symbols · 5 math rendering/codegen · 6 Claude Code integration | planned (`docs/plan/`) |

Open items: **no license chosen** (`debian/copyright` says TBD; the logo
is sahi's own design), Follow System doesn't switch on MATE (low priority,
cause unknown), and the open decisions in PLAN.md §5 (shortcut prefix `_`
vs `\`, math rendering engine, default AI provider, math index base).

## Code layout

Modules under `src/` (one folder each) are described in `ARCHITECTURE.md`
§2 and `docs/interfaces.md`. Also: `data/ui/` holds one GtkBuilder
template per widget (listed in `data/palang.gresource.xml`); `tests/` holds
the g_test programs (UI ones in the `ui_tests` list of `tests/meson.build`).
The look lives in `data/`: `style.css` + `style-dark.css`, our icons in
`data/icons/scalable/actions/`, the editor's dark scheme in
`data/styles/palang-dark.xml` (guide: `docs/05-how-the-look-works.md`).
`design/` keeps the logo's source, drafts and icon sheets (not built).
Visual changes: show sahi side-by-side screenshots before committing.

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
    for agentic features and math translation (`docs/plan/`:
    phase-3-autocomplete §4, phase-4-5-math §6, phase-6-agentic).
11. **GTK4 runs application shortcuts before the focused widget.** A
    widget that needs Ctrl+letter keys (the terminal) only gets them
    because a window key controller (`_palang_window_init_key_routing`,
    added in `palang_window_init` before the template) forwards them first
    (`commands_kept_in_terminal[]` in `palang-window-panel.c`).
12. **GApplication command lines:** the caller's environment reaches the
    running instance only with `G_APPLICATION_SEND_ENVIRONMENT`.
13. **Layout timing:** a text view created a moment ago can't scroll to a
    line yet, and a newly shown `GtkPaned` child gets its *minimum* size.
    Retry or apply on a tick callback (`gtk_widget_add_tick_callback`)
    once the widget has a size. Line positions in a new text view are
    *estimates* for a few frames: "the cursor is visible" can be true and
    then stop being true; wait until it stays so (`palang-editor.c`).
14. **GTK 4.14 list section headers:** the first header stays scrolled out
    of view when rows arrive; we show headings inside rows instead.
15. **VTE emits `child-exited` while being destroyed:** disconnect from it
    in `dispose`.
16. **GtkSourceCompletion takes keys first:** its capture-phase key
    controller on the view runs before any you add to the view, and eats
    Enter, Tab, Up, Down while the popup is open. To see them first, put
    the controller on the view's parent (`palang-lsp-completion-provider.c`).
    Its `show`/`hide` signals are requests, not emitted when the popup
    opens by itself while typing.
17. **Snippets move on with Tab only if the view's `enable-snippets` is
    on**; that also expands GtkSourceView's own snippet files ("mit" + Tab
    pastes a license), so `add_view ()` empties the snippet manager's
    search path. GtkSourceView's snippet *parser* reads its own syntax, not
    LSP's (`$1` is a reference there): `palang-lsp-snippet.c` parses LSP.
18. **Completion Details pane:** its cell's own label doesn't wrap, and a
    pane too wide to place isn't shown at all; give the cell a wrapped
    widget. An empty cell keeps the pane closed, and filling it later
    doesn't open it: show "Loading…" while asking the server.
19. **Pointers into a GVariant (`"&s"`, `JSONRPC_MESSAGE_GET_STRING`) can
    dangle** if the variant was built in memory: the first
    `g_variant_get_data ()` (which `g_variant_lookup ()` calls) turns it
    into bytes and frees the old pieces. Call `g_variant_get_data ()`
    before taking pointers, or copy strings at once
    (`palang-lsp-diagnostics.c`, found by ASan in 2c.4).
20. **`GtkListView:single-click-activate` also selects the row under the
    pointer**, and that selection stays after the pointer leaves. For
    one-click lists, put a `GtkGestureClick` on each row's contents instead
    (palette: `on_row_clicked`; file tree: `on_row_pressed`, which claims
    the click so a double click doesn't activate twice).

## Memory

Persistent memory lives in
`~/.claude/projects/-home-ali-Documents-projects-agentic-projects-Palang-IDE/memory/`
(user name, readability requirement, progress-log convention). It loads
automatically in new sessions in this folder.

## Next step

**Phase 3: AI autocomplete (ghost text).** Decided by sahi on 2026-10-22
(PLAN.md §5, decision 5). The interim look-and-feel round before it is
done (PROGRESS.md, "Interim"; guide `docs/05-how-the-look-works.md`).

1. **First, sahi's manual test results**, if they've filled them in:
   MANUAL_TESTS.md, sections "Phase 2c" and "Interim". Investigate
   failures before starting.
2. **Read** `docs/plan/phase-3-autocomplete.md` (the design), the "Phase 3"
   notes in `docs/interfaces.md` (editor/ and lsp/: Tab is already taken
   three ways; ghost text must not reach language servers), and "For later
   phases" at the end of `docs/05-how-the-look-works.md` (ghost text color
   on the dark `#0f0f0f` background, no status bar yet).
3. **Discuss with sahi before coding** (they like to decide first):
   - **Default provider** (open decision 4). This machine has **no Ollama
     and no GPU** (ThinkPad T480: Intel UHD 620, 8 threads, 31 GB RAM), so
     the design's default (local Ollama + Qwen2.5-Coder 1.5B) would run on
     the CPU: measure the latency before choosing. Installing Ollama needs
     sudo (sahi runs it). The alternative is the user's own API key
     (Anthropic Haiku, or an OpenAI-compatible service), kept in the
     keyring. Never claude.ai subscription login (gotcha 10).
   - **How ghost text is shown.** The design inserts it into the buffer
     with a tag; then undo, save, the modified flag and LSP `didChange`
     (sent from `insert-text`/`delete-range` in `PalangLspDocument`) must
     all skip it (ARCHITECTURE.md invariants). Or draw it without touching
     the buffer (e.g. GtkTextView's `snapshot_layer`). Prototype early.
   - **Who gets Tab**: completion popup, snippet, ghost text, indent.
   - **Splitting** into 3a (provider interface, adapters, settings `[ai.*]`,
     keyring via libsecret, tests against a `SoupServer` mock) and 3b
     (ghost text, triggers/debounce, accept/dismiss, budget meter, which
     needs a new status bar), as PLAN.md's size estimate suggests.
4. Libraries for it are installed: libsoup 3.4.4, libsecret 0.21.4,
   json-glib 1.8.0 (`pkg-config --modversion …`); gnome-keyring runs.

- Installed and working: clangd, pylsp + python3-pyflakes,
  typescript-language-server + TypeScript 5 (under nvm's Node 23).
- Language servers in tests: `tests/fake-lsp-server.c` (options at its
  top); window tests turn the real servers off.
