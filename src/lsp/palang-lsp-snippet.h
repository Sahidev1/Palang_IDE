/* palang-lsp-snippet.h — reading the servers' snippets.
 *
 * A completion entry can be a *snippet*: text with places the user fills
 * in, Tab moving from one to the next. Servers write them in LSP's
 * snippet syntax:
 *
 *   printf(${1:const char *format}, $2)$0
 *
 *   $1, ${1}          tab stop 1 (empty)
 *   ${1:text}         tab stop 1, with "text" filled in (may itself hold
 *                     tab stops; we keep only their text)
 *   ${1|one,two|}     tab stop 1 with a choice; we fill in the first
 *   $0                where the cursor ends
 *   $NAME, ${NAME:x}  a variable; we use its default (x) or nothing
 *   \$ \} \\          the characters themselves
 *
 * palang_lsp_snippet_parse () turns that into a list of parts: plain text
 * and tab stops. PalangLspCompletionItem builds a GtkSourceSnippet from
 * them (GtkSourceView's own parser reads a different syntax).
 * Pure functions, no GTK: unit-tested in tests/test-lsp.c.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  int   tab_stop;  /* -1: plain text; 0: where the cursor ends; 1, 2, …:
                    * places to fill in, in Tab order */
  char *text;      /* owned: the plain text, or what the tab stop starts
                    * with ("" if nothing) */
} PalangLspSnippetPart;

/* Returns: (transfer full): the parts of @snippet in order (a
 * GPtrArray of PalangLspSnippetPart, freed with it). Never fails: text
 * that isn't valid snippet syntax is kept as plain text. */
GPtrArray *palang_lsp_snippet_parse   (const char *snippet);

/* Returns: (transfer full): the text @snippet inserts when nothing is
 * filled in ("printf(${1:fmt})" → "printf(fmt)"). */
char      *palang_lsp_snippet_to_text (const char *snippet);

G_END_DECLS
