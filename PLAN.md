# Palang IDE — Plan

Owner: sahi · Drafted: 2026-10-20

## 1. Vision

A **lightweight, native, Linux-only IDE** (Debian family; developed on Linux Mint 22 / Ubuntu 24.04) for general multi-language editing, with two signature features added later:

1. **Native AI autocomplete** (Copilot-style ghost text) with selectable providers — Claude (e.g. Haiku), OpenAI-compatible APIs, or local models.
2. **Inline symbolic math** — users write formulas with math symbols (`_Sigma(x) = 1+x` → `Σ(x) = 1+x`); the editor shows the rendered formula, while generated code for it lives in the same file, hidden from view unless requested.

Agentic features (Claude Code integration) come after autocomplete.

**Non-goals:** Windows/macOS support, Electron/web UI, building our own text engine, building our own language intelligence (we reuse LSP).

---

## 2. Tech stack

| Concern | Choice | Why |
|---|---|---|
| Language | **C (C17)** with GLib/GObject | GTK's native language: all docs and examples are in C, and complete reference projects exist (see below) |
| UI toolkit | **GTK4** + **libadwaita** | Native on Linux, ~30–60 MB RAM, instant startup |
| Editor widget | **GtkSourceView 5** | Gives syntax highlighting (150+ languages), undo/redo, search, line numbers, bracket matching, themes, completion popups for free |
| Terminal (later) | **VTE** (GTK4 build) | Same terminal widget GNOME Terminal uses |
| Async | GLib main loop: `GTask`, `GCancellable`, async/finish function pairs | Network/AI calls never block the UI; no extra thread runtime to bridge |
| HTTP | **libsoup 3** (streaming responses for SSE) | Provider adapters; runs on the GLib main loop |
| JSON | **json-glib** | AI API requests/responses, symbol table |
| JSON-RPC (LSP) | **jsonrpc-glib** | Same library GNOME Builder uses for its LSP client |
| Config | `GKeyFile` (INI-style) in `~/.config/palang/config.ini` | Built into GLib, human-editable |
| Secrets | **libsecret** | API keys stored in the system keyring, never in config files |
| Build | **Meson** + Ninja | GNOME standard; easy sanitizer/test integration |
| Packaging | debhelper (`dpkg-buildpackage`) with Meson → single `.deb` | Debian-only target |

**System packages (Mint 22 / Ubuntu 24.04):** `build-essential`, `meson`, `ninja-build`, `pkg-config`, `libgtk-4-dev` (4.14), `libgtksourceview-5-dev` (5.12), `libadwaita-1-dev` (1.5), `libvte-2.91-gtk4-dev`, `libsoup-3.0-dev`, `libjson-glib-dev`, `libjsonrpc-glib-1.0-dev`, `libsecret-1-dev`, `valgrind`, `debhelper`.

**Reference code to learn from / borrow patterns from:**
- **GNOME Text Editor** (C, GTK4, GtkSourceView 5): tabs, documents, session restore, preferences.
- **GNOME Builder** (C, GTK4): full IDE — LSP client, terminal, project tree, diagnostics.
- **gedit** (C): classic GtkSourceView editor.

**Rejected:** Electron (heavy), Tauri (WebKitGTK is slow on Linux, Monaco feels sluggish), Code-OSS fork (huge codebase, not lightweight), Rust (sahi doesn't know it; GTK is one of the harder areas of Rust). Trade-off accepted: no Monaco means we build inline ghost text ourselves.

---

## 3. Architecture and conventions

See [ARCHITECTURE.md](ARCHITECTURE.md): modules, dependency rules,
invariants, code conventions, and how phases and sessions work. What each
existing module offers: [docs/interfaces.md](docs/interfaces.md).

---

## 4. Roadmap

Each phase's design lives in `docs/plan/`; a session reads only its own.
"Session" = one Claude Code session (see ARCHITECTURE.md §5).

| Phase | What | Design | Sessions | State |
|---|---|---|---|---|
| 0 | Project setup, first window | [phase-0-1](docs/plan/phase-0-1.md) | — | ✅ |
| 1 | Usable editor: tabs, file tree, find, settings, session, `.deb` | [phase-0-1](docs/plan/phase-0-1.md) | — | ✅ |
| 2a | Terminal, project search, command palette | [phase-2](docs/plan/phase-2.md) | 1 | next |
| 2b | LSP client | [phase-2](docs/plan/phase-2.md) | 1 | |
| 3 | AI autocomplete (ghost text) | [phase-3-autocomplete](docs/plan/phase-3-autocomplete.md) | 1 | |
| 4 | Math symbols: shortcuts + palette | [phase-4-5-math](docs/plan/phase-4-5-math.md) §1–2 | 1, or bundled with the start of 5 | |
| 5 | Inline math rendering + hidden generated code | [phase-4-5-math](docs/plan/phase-4-5-math.md) §3–7 | 1–2 | |
| 6 | Agentic (Claude Code) | [phase-6-agentic](docs/plan/phase-6-agentic.md) | 1 | |

Phases 3 and 4 are independent and can swap order. Phase 2 comes before
Phase 5 because language-server types feed math code generation.

---

## 5. Open decisions

1. ~~Implementation language~~ — **decided: C** (sahi knows C; Claude writes most code, sahi reviews).
2. **Shortcut prefix** — `_` (with the known-name rule) vs `\`.
3. **Math rendering engine** — custom GSK layout vs MicroTeX FFI (decide at Phase 5 stage 2).
4. **Default AI provider** — local Ollama recommended; confirm whether to ship a "Claude Haiku" preset.
5. **Phase order** — autocomplete (3) before math (4–5), or the reverse.
6. **Math index base** — default "math index 1 = array index 0" with `@index 0` to override (`docs/plan/phase-4-5-math.md` §7); confirm before Phase 5.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Ghost text leaks into saves/undo/LSP | Single save path that skips ghost-text regions; tests for save/undo with a suggestion visible |
| Hidden code is wrong and nobody notices | Deterministic Tier 1 first, approval for AI output, hash checks, "show code" toggle, status icons |
| Generated math code compiles but is wrong (integer division, off-by-one indexing, wrong types) | Typed literals, the explicit index rule, language-server types and diagnostics shown on the formula, unit tests per target language for each trap in `phase-4-5-math.md` §7 |
| Cursor movement into invisible regions behaves oddly | Treat hidden blocks as atomic; handle cursor keys explicitly around anchors; test early with a spike |
| AI cost surprises | Bring-your-own key, budget cap, usage meter, local default |
| Memory bugs in C (use-after-free, leaks, overruns) | Sanitizers always on in debug builds, `g_autoptr`, GLib containers, valgrind before releases, unit tests run under ASan |
| GTK text widget performance on huge files | Disable heavy features above a size threshold; profile with 50k+ line files |

## 7. Verification

Per phase: in that phase's design file. For every phase:

- **Every phase:** `meson test -C build` passes under ASan/UBSan with zero sanitizer reports; a short valgrind run of the app shows no new definite leaks.
- Unit tests (`g_test`): symbol expansion, math parser, codegen output per language, marker parsing/hashing, provider adapters (against a local mock HTTP server built with `SoupServer`).
