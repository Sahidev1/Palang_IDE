# Palang IDE — Architecture

The rules every phase follows. Short on purpose: read it once per session.
What each module offers is in [docs/interfaces.md](docs/interfaces.md);
what to build next is in [PLAN.md](PLAN.md) and `docs/plan/`.

## 1. Modular phased design

Palang is built in **phases** (see PLAN.md). Each phase adds **modules**
with a clear public interface, and later phases build on those interfaces
instead of on the code behind them.

Why: the code should stay easy to read and change, and each Claude Code
session should be able to start a phase by reading interfaces, not
implementations. Every file read stays in the session's context for the
rest of the session, so this saves usage too.

## 2. Modules

One folder per module under `src/`. A module is one or more GObject types
working together on one concern.

| Module | Concern | Phase |
|---|---|---|
| `settings/` | preferences (`config.ini`) and applying appearance | 1 |
| `editor/` | one open file: document, its editor widget, find bar | 1 |
| `workspace/` | the project folder: file tree, directory listing | 1 |
| `app/` | application, window, session; **connects the modules** | 1 |
| `terminal/` | integrated terminal panel | 2a |
| `search/` | project-wide search | 2a |
| `palette/` | command palette | 2a |
| `lsp/` | language server client | 2b |
| `ai/` | completion providers, ghost text | 3 |
| `math/` | symbol table, math parser, codegen, math blocks | 4–5 |
| `integrations/` | Claude Code IDE integration | 6 |

### Rules

1. **A module's public headers are its contract.** Other modules use only
   what the `.h` files declare. Anything else is `static` in the `.c`.
2. **Dependencies point one way:**
   ```
   app  →  everything else
   editor, workspace, terminal, search, palette, lsp, ai, math  →  settings
   lsp, ai, math  →  editor (they work on documents and views)
   settings  →  nothing of ours
   ```
   Feature modules don't include each other's headers unless the line
   above allows it. If two feature modules need to cooperate, `app/` wires
   them together (connects a signal of one to a function of the other).
   The build doesn't enforce this (`src/meson.build` puts every folder on
   the include path), so watch for it in review.
3. **Modules talk through GObject signals and properties**, not by reaching
   into each other's structs. A module announces things ("file-activated",
   "changed-on-disk"); whoever cares connects to them.
4. **`app/` composes, it doesn't implement.** The window places widgets,
   registers actions and connects modules. A new feature is a new module
   (or a new type in one) that the window adds, not more code in
   `palang-window.c` (already ~1000 lines). When a part of the window grows
   big, it moves out into its own type.
5. **One owner per resource.** All file I/O goes through `PalangDocument`;
   all preferences through `PalangSettings`; later, all network calls for
   AI through the `ai/` provider interface and all language-server traffic
   through `lsp/`.
6. **Every module can be tested on its own.** Its tests use its public
   interface, and they double as examples of how to use it.

## 3. Invariants (must stay true in every phase)

- **All file I/O goes through `PalangDocument`.** Later features (hidden
  regions, ghost text) hook in there.
- **Hidden code stays in the buffer.** Math code is hidden with an
  `invisible` text tag, not removed, so saving writes the buffer as-is.
- **Ghost text is never saved, never enters undo history, never sent to a
  language server.**
- **The UI thread never blocks.** Network: libsoup async. Subprocesses:
  `GSubprocess` async. CPU-heavy work: `g_task_run_in_thread`. Results come
  back on the main loop.
- **Never lose the user's work.** Anything that can discard text asks
  first (see the save flow in `app/`).
- **API keys live in the keyring** (libsecret), never in `config.ini`.

## 4. Code conventions (readability first)

Claude writes most of the code; sahi must be able to read and understand
all of it.

- **GNOME C style:** `palang_` prefix for public symbols, one GObject type
  per `.c`/`.h` pair (`PalangEditor` → `palang-editor.c`/`.h`),
  `G_DECLARE_FINAL_TYPE`. Layout of a widget in `data/ui/<file>.ui`.
- **Plain C over clever C:** no custom macros beyond the standard GObject
  ones; small functions; early returns.
- **Every file starts with a short comment** saying what it does and how it
  fits in. Every public function has a comment in its header (ownership:
  `transfer full` / `transfer none`, `nullable`).
- **Comments explain *why*,** and introduce a GTK/GLib pattern the first
  time it appears.
- **Memory:** `g_autoptr` / `g_autofree`; GLib containers instead of raw
  arrays and `strcpy`.
- **Errors:** `GError **` out-parameters, never silently ignored.
- **Safety net:** debug builds use ASan + UBSan and `-Wall -Wextra
  -Werror`; valgrind before releases.

## 5. Phases and sessions

- **One commit per step** within a phase.
- **End of each phase**, committed together: a `PROGRESS.md` entry,
  a `MANUAL_TESTS.md` section, a `docs/NN-*.md` "how it works" guide, and
  **an updated `docs/interfaces.md`** (new modules, changed interfaces,
  extension points left for later phases).
- **A Claude session usually covers one phase** (or half of a big one, like
  2a/2b). It starts by reading `CLAUDE.md`, this file, `docs/interfaces.md`
  and the phase's plan in `docs/plan/`, and opens implementation files only
  for the modules it changes.
- **`docs/interfaces.md` points to headers instead of copying them**, so it
  can't drift from the code. It adds only what a header can't say: how
  modules fit together, rules, and where later phases plug in.
