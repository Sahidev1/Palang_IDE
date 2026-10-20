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

## 2b. Code conventions (readability first)

Claude writes most of the code; sahi must be able to read and understand all of it. So:

- **GNOME C style:** `palang_` prefix for all public symbols, one GObject type per `.c`/`.h` pair (`PalangEditor` → `palang-editor.c`/`.h`), `G_DECLARE_FINAL_TYPE` for types.
- **Plain C over clever C:** no custom macros beyond the standard GObject ones; small functions; early returns.
- **Every file starts with a short comment** explaining what it does and how it fits in.
- **Comments explain *why*,** and explain any non-obvious GTK/GLib pattern the first time it appears (e.g. the async/finish pattern, signal connections, reference ownership).
- **Memory:** `g_autoptr` / `g_autofree` for automatic cleanup; GLib containers (`GString`, `GPtrArray`, `GHashTable`) instead of raw arrays and `strcpy`; ownership noted in function docs (`transfer full` / `transfer none`).
- **Errors:** `GError **` out-parameters, never silently ignored.
- **Safety net:** debug builds always use AddressSanitizer + UndefinedBehaviorSanitizer (`-Db_sanitize=address,undefined`), `-Wall -Wextra -Werror`; valgrind before releases.
- **Commits are small** and each phase ends with a short "how this works" note in `docs/` for sahi.

---

## 3. Architecture

```
palang/
├── meson.build
├── meson_options.txt
├── src/
│   ├── main.c                        starts the PalangApplication
│   ├── app/
│   │   ├── palang-application.c/.h   GtkApplication subclass, actions, startup
│   │   └── palang-window.c/.h        header bar, layout (paned: tree | tabs | panel)
│   ├── editor/
│   │   ├── palang-editor.c/.h        wrapper around GtkSourceView
│   │   ├── palang-document.c/.h      load/save, encoding, dirty tracking (GtkSourceFile)
│   │   ├── palang-ghost-text.c/.h    (phase 3) inline suggestion rendering
│   │   └── palang-math-view.c/.h     (phase 5) math block hiding / rendering
│   ├── workspace/
│   │   └── palang-file-tree.c/.h     file tree, project root, file watching (GFileMonitor)
│   ├── tabs/                         tab management, session restore
│   ├── settings/                     config loading (GKeyFile), keybindings, themes
│   ├── ai/                           (phase 3) provider interface + adapters, request scheduler
│   ├── math/                         (phase 4–5) symbol table, parser, codegen, markers
│   └── integrations/                 (phase 6) Claude Code, LSP client
├── data/
│   ├── symbols.json                  math symbol table (user-extendable)
│   ├── palang.gresource.xml          UI files, CSS bundled into the binary
│   ├── ui/*.ui                       GtkBuilder UI definitions
│   └── palang.desktop, icons
├── tests/                            g_test unit tests
├── docs/                             "how this works" notes per phase
└── debian/                           packaging
```

**Key principles (set up in the MVP, even before AI/math exist):**
- **All file I/O goes through `PalangDocument`.** Later features (hidden regions, inline widgets, ghost text) hook in here.
- **Hidden code stays in the buffer.** Math code is hidden with a GtkTextTag `invisible` property rather than removed, so *save = buffer text as-is*. No complicated mapping between what's displayed and what's on disk.
- **Ghost text must never be saved, never enter undo history, never be sent to LSP.** Saving and change notifications go through code that skips ghost-text regions.
- **No blocking on the UI thread.** Network work uses libsoup's async API; subprocesses use `GSubprocess` async; CPU-heavy work (project search, parsing big files) runs in `g_task_run_in_thread`. Results always come back on the main loop.

---

## 4. Roadmap

### Phase 0 — Project setup
- Install system deps; create the Meson project with the directory layout above.
- "Hello window": `PalangApplication` + `PalangWindow` with a single GtkSourceView.
- Debug build with sanitizers on by default: `meson setup build -Dbuildtype=debug -Db_sanitize=address,undefined`, `ninja -C build`, `meson test -C build`.
- `docs/00-how-gtk-apps-work.md`: short primer on GObject, signals, reference counting and the async/finish pattern, written for sahi.

### Phase 1 — MVP: editor + file tree + tabs
- **Window layout:** header bar, left file tree, center tab view (`adw::TabView`/`TabBar`), optional bottom panel placeholder.
- **File tree:** open folder, lazy-loaded `GtkTreeListModel` + `GtkListView` over `GtkDirectoryList`, icons, hidden-file toggle, refresh on disk changes (`GFileMonitor`).
- **Tabs:** `AdwTabView` + `AdwTabBar`; open/close/reorder, dirty indicator, "unsaved changes" prompt on close/quit.
- **Editor:** language auto-detect (`GtkSourceLanguageManager`), style schemes, line numbers, current-line highlight, auto-indent, bracket matching, tab width/spaces settings.
- **File ops:** New, Open, Save, Save As, Save All (`GtkSourceFileLoader` / `GtkSourceFileSaver`, async), reload when changed on disk.
- **Search:** find/replace in file (`GtkSourceSearchContext`), go to line.
- **Theme (light/dark):** a **System / Light / Dark** setting (`AdwStyleManager:color-scheme`). "System" uses the desktop's dark-mode preference when it reports one. Some desktops don't: MATE (sahi's desktop) reports *no preference* and switches themes only by GTK theme name, which libadwaita ignores (found in Phase 0 manual test 2). Fallback: when the desktop reports no preference, watch `GtkSettings:gtk-theme-name` (MATE updates it live) and use dark when the name contains "dark".
- **Settings:** `config.ini` via `GKeyFile` (font, theme, tab size, keybindings), basic preferences dialog (`AdwPreferencesDialog`).
- **Session restore:** reopen last folder and tabs.
- **Packaging:** first `.deb` with desktop entry and icon.

### Phase 2 — Editor essentials
- Integrated terminal panel (VTE).
- Project-wide search: use `rg` (ripgrep) through `GSubprocess` when installed, with a fallback that walks files with `GFileEnumerator` in a worker thread (respecting `.gitignore` in a basic way).
- Command palette (Ctrl+Shift+P).
- **LSP client** (`jsonrpc-glib` over the server's stdio via `GSubprocess`): diagnostics, hover, go-to-definition, popup completions through a `GtkSourceCompletionProvider`. Servers configured per language (`clangd`, `pyright`, `rust-analyzer`, …). GNOME Builder's LSP code is the reference.

### Phase 3 — AI autocomplete (ghost text)
See §5.

### Phase 4 — Math symbols: shortcuts + palette
See §6.1–6.2.

### Phase 5 — Inline math rendering + hidden generated code
See §6.3–6.6.

### Phase 6 — Agentic (Claude Code)
See §7.

Phases 3 and 4 are independent and can swap order.

---

## 5. AI autocomplete (Phase 3)

### 5.1 Providers
A single GObject interface; users choose a provider + model **per role** (`completion`, later `chat`/`math`).

```c
/* palang-completion-provider.h */
G_DECLARE_INTERFACE (PalangCompletionProvider, palang_completion_provider,
                     PALANG, COMPLETION_PROVIDER, GObject)

typedef struct {
  const char *prefix;      /* text before the cursor */
  const char *suffix;      /* text after the cursor */
  const char *language;    /* e.g. "python" */
  const char *path;
  guint       max_tokens;
} PalangCompletionRequest;

/* Called for each streamed piece of text, on the main loop. */
typedef void (*PalangChunkFunc) (const char *chunk, gpointer user_data);

struct _PalangCompletionProviderInterface {
  GTypeInterface parent_iface;

  void  (*complete_async)  (PalangCompletionProvider      *self,
                            const PalangCompletionRequest *request,
                            PalangChunkFunc                on_chunk,
                            gpointer                       chunk_data,
                            GCancellable                  *cancellable,
                            GAsyncReadyCallback            callback,
                            gpointer                       user_data);
  char *(*complete_finish) (PalangCompletionProvider *self,
                            GAsyncResult             *result,
                            GError                  **error);
};
```

Cancelling = `g_cancellable_cancel()`; libsoup aborts the HTTP request.

Adapters (three cover almost everything):
1. **Anthropic Messages API** — Claude models (e.g. Haiku). Prompted to act as a fill-in-the-middle model; prompt caching on the stable prefix.
2. **OpenAI-compatible chat API** — OpenAI, OpenRouter, Mistral, DeepSeek, Groq, LM Studio, llama.cpp server, Ollama.
3. **Native FIM endpoints** — Ollama `/api/generate` with `suffix`, Codestral FIM, DeepSeek FIM. Code models trained for fill-in-the-middle are usually faster, cheaper and better at autocomplete than chat models.

**Default:** local **Ollama + Qwen2.5-Coder (1.5B/7B)** — free, private, often <200 ms on a GPU. Cloud providers are opt-in.

Example `config.ini`:
```ini
[ai.completion]
# anthropic | openai-compatible | fim | ollama
provider=ollama
model=qwen2.5-coder:1.5b
base-url=http://localhost:11434

[ai.provider.anthropic]
# key stored in the keyring (libsecret), not here
model=claude-haiku-5-5

[ai.limits]
daily-token-budget=2000000
# auto | manual
trigger=auto
```

### 5.2 Ghost text rendering
- Insert the suggestion at the cursor with a gray `ghost` tag; track its range.
- **Tab** accepts it (remove the tag, keep the text); **Esc**, cursor movement or any non-matching key removes it.
- If the user types characters that match the suggestion, consume them and keep showing the rest (no new request).
- Insert/remove ghost text inside an irreversible action / with undo suppressed so it never reaches the undo stack; exclude it from save, dirty state and LSP `didChange`.
- Optional later: Ctrl+→ accepts word by word.

### 5.3 Keeping usage reasonable
- **Bring your own key:** users pay for their own usage.
- **Debounce** about 300 ms after typing stops; **cancel** in-flight requests on new keystrokes.
- **Smart triggers:** after newline, `(`, `=`, `:`, `{`, or at end of line; not in the middle of a word, not inside strings/comments (unless configured).
- **Small context:** about 1–2k tokens around the cursor plus file header/imports; `max_tokens` 64–128; stop at blank line / end of block.
- **Local cache** keyed by (prefix tail, suffix head).
- **Prompt caching** for stable parts (Anthropic).
- **Status bar meter:** requests and tokens today, estimated cost; daily budget pauses autocomplete when reached.
- **Manual mode:** Alt+\ to request a suggestion.
- Rough scale: an active hour ≈ 300–1000 requests ≈ 0.5–1.5M input tokens. Check current model pricing before choosing defaults.

### 5.4 Not used for autocomplete
Claude Code / the Agent SDK: too slow for per-keystroke suggestions (seconds, not ~200 ms). Don't build on claude.ai subscription login in a third-party product; Anthropic expects API keys unless it has approved otherwise. Re-check current terms.

---

## 6. Inline symbolic math (Phases 4–5)

### 6.1 Symbol shortcuts
- Typing `_Name` followed by a non-letter expands to the symbol: `_Sigma(` → `Σ(`, `_sigma` → `σ`, `_sum` → `∑`, `_int` → `∫`, `_leq` → `≤`, `_to` → `→`.
- **Case-sensitive** (`_Sigma` Σ vs `_sigma` σ).
- **Expansion happens only inside math regions** so normal code (`my_pi`, `_private`) is never touched.
- **Ctrl+Z right after an expansion** restores the typed text.
- **Completion popup** while typing (`_Si…` → list of Σ Sigma, σ sigma, ∼ sim) via a GtkSourceView completion provider.
- **Letters and operators are separate entries:** Σ (U+03A3, Greek letter: a name like a function `Σ(x)`) vs ∑ (U+2211, summation operator). This matters for code generation.
- **Open decision:** `_` is also the natural subscript marker (`x_i`). Rule proposal: `_` + a *known symbol name* = shortcut, otherwise subscript. Alternative: use `\` as the shortcut prefix (Julia/LaTeX style). Decide before Phase 4.

### 6.2 Symbol palette (virtual keyboard)
- `GtkPopover` with a `GtkFlowBox` grid, opened by Ctrl+M or a toolbar button.
- Tabs: Greek · Operators (∑ ∏ ∫ ∂ ∇ √ ±) · Relations (≤ ≥ ≠ ≈ ≡ ∝) · Sets & logic (∈ ∉ ⊂ ∪ ∩ ∀ ∃ ¬ ∧ ∨) · Arrows (→ ⇒ ↔ ↦) · Structures (fraction, power, subscript, root, matrix templates).
- Hover shows name + shortcut ("Σ — `_Sigma`") so users learn to type them.
- Search box ("integral" → ∫ ∬ ∮), recently-used row.
- **Symbol table in `data/symbols.json`** (name, glyph, kind = letter/operator/relation…, ASCII name for codegen), loaded with json-glib into a `GHashTable`. Start with about 200 curated entries; users can extend it in `~/.config/palang/symbols.json`. A larger set can be generated from Julia's completion table / LaTeX `unicode-math`.

### 6.3 Linear input syntax
Borrow from Microsoft's **UnicodeMath** linear format:
- `_` subscript, `^` superscript, `/` fraction, `( )` grouping, `√( )` root, `_sum_(i=1)^n`.
- Example: `∑_(i=1)^n x_i^2 / n`.
- A small hand-written parser in `math/palang-math-parser.c` (recursive descent, works on UTF-8 via `g_utf8_*` functions) → our own math AST (a tree of `PalangMathNode` structs). This AST feeds both rendering and codegen.

### 6.4 File format (in-file markers)
Everything lives in the source file; the comment prefix comes from GtkSourceView language metadata (`line-comment-start`).

```python
# @math Σ(x) = 1 + x  @f:3fa9c2 @c:81d0e4
def Σ(x):
    return 1 + x
# @end
```

- The formula is stored in **Unicode linear form**, so it's readable in any editor, on GitHub and in diffs.
- `@f` = hash of the normalized formula + target language + generator version; `@c` = hash of the generated code.
  - Formula hash mismatch → regenerate.
  - Code hash mismatch → code was edited outside the formula (e.g. in another editor) → warn, offer "regenerate" or "keep edit".
- The file always compiles/runs anywhere; other tools just see comments + normal code.

### 6.5 Display in the editor
- The `@math` marker line, the code, and `@end` get the `invisible` tag.
- A `TextChildAnchor` at the block start holds the **rendered formula widget** plus a status icon: ✓ up to date · ⟳ generating · ⚠ failed/stale.
- Click the formula → edit mode (shows the raw linear text); leaving edit mode re-renders and triggers generation.
- "Show generated code" toggle per block and globally (also on hover).
- **Rendering stages:**
  1. Unicode text with Pango sub/superscript markup (cheap, enough at first).
  2. A real 2D layout (fractions, stacked limits, roots): our own layout engine drawn with GSK/Cairo from the math AST, or MicroTeX via FFI.

### 6.6 Formula → code translation
Runs when the user **finishes** an expression (leaves the block / Enter / command), never per keystroke. If the hash is unchanged, nothing happens.

```
formula changed
  → Tier 1: parser → AST → deterministic codegen → write hidden code (auto)
  → parse failed / ambiguous → Tier 2: AI backend → show diff → user accepts
```

**Tier 1, deterministic (no AI, free, offline):**
- Start with **our own C codegen** from the AST (one small emitter per target language): arithmetic, function definitions, sums/products as loops/iterators, powers, roots, common functions — for Python, Rust, C/C++, JavaScript.
- Later, for symbolic work (derivatives, integrals, simplification): a **SymPy sidecar** — a small Python helper (`python3-sympy` from apt) started with `GSubprocess`, talking JSON over stdin/stdout; use SymPy's printers (`pycode`, `ccode`, `rust_code`, `jscode`, …).

**Tier 2, AI (context-dependent cases):**
- Is `x` a scalar or an array? Is `xy` a product or one name? Which existing variables/functions to use? Unusual notation.
- Sends formula + surrounding code through the **same provider layer** as autocomplete (API key or local Ollama).
- **Claude Code works here:** speed doesn't matter for this step, so the IDE can run the user's own installed `claude -p "…"` (their own login) as an optional backend.
- **AI output is never accepted silently**: the user approves a diff once, because they won't see the code later.

**Naming rules per project:** keep Unicode identifiers (`Σ`, fine in Python/Rust) or convert them to ASCII (`Σ` → `Sigma`) for C/C++ or strict style guides. Operators (∑, ∫) always become code, never identifiers.

**Example:**
| Formula | Python | Rust |
|---|---|---|
| `Σ(x) = 1 + x` | `def Σ(x): return 1 + x` | `fn sigma(x: f64) -> f64 { 1.0 + x }` |
| `∑_(i=1)^n x_i^2` | `sum(x[i]**2 for i in range(1, n+1))` | `(1..=n).map(\|i\| x[i].powi(2)).sum::<f64>()` |

---

## 7. Agentic features (Phase 6)

- Run the user's own installed **Claude Code** (`claude`) in the integrated terminal, under their own login.
- Implement Claude Code's **IDE integration** so `claude` detects Palang: a local server the CLI connects to, letting it open diffs in our editor and read the current selection, open files and diagnostics. *Verify the current protocol in the docs first; existing third-party integrations (e.g. for Neovim) are useful references.*
- Diff review UI: side-by-side or inline diff with accept/reject per hunk.
- Later: an optional chat panel through the provider layer (`chat` role) for non-Claude-Code users.

---

## 8. Open decisions

1. ~~Implementation language~~ — **decided: C** (sahi knows C; Claude writes most code, sahi reviews).
2. **Shortcut prefix** — `_` (with the known-name rule) vs `\`.
3. **Math rendering engine** — custom GSK layout vs MicroTeX FFI (decide at Phase 5 stage 2).
4. **Default AI provider** — local Ollama recommended; confirm whether to ship a "Claude Haiku" preset.
5. **Phase order** — autocomplete (3) before math (4–5), or the reverse.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Ghost text leaks into saves/undo/LSP | Single save path that skips ghost-text regions; tests for save/undo with a suggestion visible |
| Hidden code is wrong and nobody notices | Deterministic Tier 1 first, approval for AI output, hash checks, "show code" toggle, status icons |
| Cursor movement into invisible regions behaves oddly | Treat hidden blocks as atomic; handle cursor keys explicitly around anchors; test early with a spike |
| AI cost surprises | Bring-your-own key, budget cap, usage meter, local default |
| Memory bugs in C (use-after-free, leaks, overruns) | Sanitizers always on in debug builds, `g_autoptr`, GLib containers, valgrind before releases, unit tests run under ASan |
| GTK text widget performance on huge files | Disable heavy features above a size threshold; profile with 50k+ line files |

## 10. Verification

- **Phase 1:** open a real project folder; edit/save/reopen files in several languages; close with unsaved changes → prompt; external modification → reload prompt; restart → session restored; install the `.deb` with `sudo apt install ./palang_*.deb` and launch from the menu.
- **Phase 2:** `clangd`/`pyright` diagnostics and go-to-definition work; terminal runs a shell.
- **Phase 3:** with Ollama running, suggestions appear within ~300 ms of a pause; Tab accepts; Esc/typing dismisses; save while a suggestion is visible → the file contains no ghost text; undo never restores ghost text; token meter increments; budget cap pauses requests. Repeat with Anthropic and an OpenAI-compatible provider.
- **Phase 4:** `_Sigma(` → `Σ(` inside a math block, untouched outside; Ctrl+Z reverts; palette inserts symbols; custom symbols from the user TOML appear.
- **Phase 5:** write `Σ(x) = 1 + x` → generated code hidden, file runs with `python3`; the file opened in another editor shows comment + code; editing the code externally → ⚠ stale on reopen; AI fallback shows a diff before inserting.
- **Phase 6:** `claude` in the terminal connects to Palang and opens diffs in the editor.
- **Every phase:** `meson test -C build` passes under ASan/UBSan with zero sanitizer reports; a short valgrind run of the app shows no new definite leaks.
- Unit tests (`g_test`): symbol expansion, math parser, codegen output per language, marker parsing/hashing, provider adapters (against a local mock HTTP server built with `SoupServer`).
