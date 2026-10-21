/* palang-lsp-position.h — converting between editor and LSP positions.
 *
 * Both count lines from 0, but within a line they count differently:
 *
 *   - GtkTextBuffer counts *characters* (Unicode code points).
 *   - LSP counts *UTF-16 code units* (we offer only "utf-16" in
 *     "initialize"). Most characters are one unit, but those outside the
 *     Basic Multilingual Plane (emoji, some math letters like 𝑥) are two.
 *
 * So "a😀b": the b is character 2 for GTK and character 3 for LSP. Every
 * position going to or coming from a server goes through these functions.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Returns: how many UTF-16 code units @text (valid UTF-8, NUL-terminated)
 * takes. */
guint palang_lsp_utf16_length       (const char *text);

/* Returns: how many characters of @text (valid UTF-8, NUL-terminated)
 * come before the UTF-16 position @utf16_column. A position past the end
 * gives the length of @text; one in the middle of a two-unit character
 * gives that character's start. */
guint palang_lsp_chars_for_utf16    (const char *text,
                                     guint       utf16_column);

/* The LSP position (@line, @character) of @iter. */
void  palang_lsp_position_from_iter (const GtkTextIter *iter,
                                     guint             *line,
                                     guint             *character);

/* Sets @iter to the LSP position (@line, @character) in @buffer. A
 * character past the end of the line gives the line's end; a line past
 * the end of the buffer gives the buffer's end. */
void  palang_lsp_iter_from_position (GtkTextBuffer *buffer,
                                     guint          line,
                                     guint          character,
                                     GtkTextIter   *iter);

G_END_DECLS
