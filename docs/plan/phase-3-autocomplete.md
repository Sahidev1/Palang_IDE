# Phase 3 — AI autocomplete (ghost text)

Design for Phase 3 (moved out of PLAN.md, where it was §5). Rules that apply
to every phase are in ARCHITECTURE.md.

## 1. Providers
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

## 2. Ghost text rendering
- Insert the suggestion at the cursor with a gray `ghost` tag; track its range.
- **Tab** accepts it (remove the tag, keep the text); **Esc**, cursor movement or any non-matching key removes it.
- If the user types characters that match the suggestion, consume them and keep showing the rest (no new request).
- Insert/remove ghost text inside an irreversible action / with undo suppressed so it never reaches the undo stack; exclude it from save, dirty state and LSP `didChange`.
- Optional later: Ctrl+→ accepts word by word.

## 3. Keeping usage reasonable
- **Bring your own key:** users pay for their own usage.
- **Debounce** about 300 ms after typing stops; **cancel** in-flight requests on new keystrokes.
- **Smart triggers:** after newline, `(`, `=`, `:`, `{`, or at end of line; not in the middle of a word, not inside strings/comments (unless configured).
- **Small context:** about 1–2k tokens around the cursor plus file header/imports; `max_tokens` 64–128; stop at blank line / end of block.
- **Local cache** keyed by (prefix tail, suffix head).
- **Prompt caching** for stable parts (Anthropic).
- **Status bar meter:** requests and tokens today, estimated cost; daily budget pauses autocomplete when reached.
- **Manual mode:** Alt+\ to request a suggestion.
- Rough scale: an active hour ≈ 300–1000 requests ≈ 0.5–1.5M input tokens. Check current model pricing before choosing defaults.

## 4. Not used for autocomplete
Claude Code / the Agent SDK: too slow for per-keystroke suggestions (seconds, not ~200 ms). Don't build on claude.ai subscription login in a third-party product; Anthropic expects API keys unless it has approved otherwise. Re-check current terms.

## 5. Verification

With Ollama running, suggestions appear within ~300 ms of a pause; Tab accepts; Esc/typing dismisses; save while a suggestion is visible → the file contains no ghost text; undo never restores ghost text; token meter increments; budget cap pauses requests. Repeat with Anthropic and an OpenAI-compatible provider.
