/* palang-lsp-markdown.h — the servers' Markdown as text for a GtkLabel.
 *
 * Language servers write hover text (and later completion documentation)
 * in Markdown. We show it as plain text with a few touches, by turning it
 * into Pango markup (the <b>, <i>, <tt> tags GtkLabel understands):
 *
 *   ```lang … ```     code block      → monospace lines
 *   `code`            inline code     → monospace
 *   ### Title         heading         → bold line
 *   ---               separator       → empty line
 *   **bold**, *it*    emphasis        → bold, italic
 *   \*                escaped "*"     → *
 *
 * Everything else (links, lists, tables, "_" emphasis) stays as written.
 * Pure functions, no GTK: unit-tested in tests/test-lsp.c.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Returns: (transfer full): @markdown as Pango markup, with empty lines
 * at the start and end removed and runs of empty lines made one. Always
 * valid markup, whatever @markdown contains. */
char *palang_lsp_markdown_to_markup (const char *markdown);

/* Returns: (transfer full): @text written as Markdown that shows exactly
 * @text: every ASCII punctuation character gets a backslash. For servers
 * that answer in plain text. */
char *palang_lsp_markdown_escape    (const char *text);

G_END_DECLS
