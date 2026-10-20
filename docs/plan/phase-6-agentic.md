# Phase 6 — Agentic features (Claude Code)

Design for Phase 6 (moved out of PLAN.md, where it was §7).

- Run the user's own installed **Claude Code** (`claude`) in the integrated terminal, under their own login.
- Implement Claude Code's **IDE integration** so `claude` detects Palang: a local server the CLI connects to, letting it open diffs in our editor and read the current selection, open files and diagnostics. *Verify the current protocol in the docs first; existing third-party integrations (e.g. for Neovim) are useful references.*
- Diff review UI: side-by-side or inline diff with accept/reject per hunk.
- Later: an optional chat panel through the provider layer (`chat` role) for non-Claude-Code users.

## Verification

`claude` in the terminal connects to Palang and opens diffs in the editor.
