/* palang-lsp-position.c — see palang-lsp-position.h.
 *
 * A character needs two UTF-16 code units (a "surrogate pair") when its
 * code point is above U+FFFF.
 */

#include "config.h"

#include "palang-lsp-position.h"

static guint
utf16_units (gunichar c)
{
  return c > 0xFFFF ? 2 : 1;
}

guint
palang_lsp_utf16_length (const char *text)
{
  guint length = 0;

  g_return_val_if_fail (text != NULL, 0);

  for (const char *p = text; *p != '\0'; p = g_utf8_next_char (p))
    length += utf16_units (g_utf8_get_char (p));

  return length;
}

guint
palang_lsp_chars_for_utf16 (const char *text,
                            guint       utf16_column)
{
  guint units = 0;
  guint chars = 0;

  g_return_val_if_fail (text != NULL, 0);

  for (const char *p = text; *p != '\0'; p = g_utf8_next_char (p))
    {
      units += utf16_units (g_utf8_get_char (p));
      /* Only count a character if the position is at or after its end. */
      if (units > utf16_column)
        break;
      chars++;
    }

  return chars;
}

void
palang_lsp_position_from_iter (const GtkTextIter *iter,
                               guint             *line,
                               guint             *character)
{
  GtkTextIter line_start = *iter;
  g_autofree char *before = NULL;

  g_return_if_fail (iter != NULL);

  gtk_text_iter_set_line_offset (&line_start, 0);
  /* A "slice" includes text hidden with an invisible tag: the server sees
   * all of the text, hidden or not. */
  before = gtk_text_iter_get_slice (&line_start, iter);

  *line = gtk_text_iter_get_line (iter);
  *character = palang_lsp_utf16_length (before);
}

void
palang_lsp_iter_from_position (GtkTextBuffer *buffer,
                               guint          line,
                               guint          character,
                               GtkTextIter   *iter)
{
  GtkTextIter line_end;
  g_autofree char *text = NULL;

  g_return_if_fail (GTK_IS_TEXT_BUFFER (buffer));
  g_return_if_fail (iter != NULL);

  if (line > G_MAXINT || !gtk_text_buffer_get_iter_at_line (buffer, iter, (int) line))
    {
      gtk_text_buffer_get_end_iter (buffer, iter);
      return;
    }

  line_end = *iter;
  if (!gtk_text_iter_ends_line (&line_end))
    gtk_text_iter_forward_to_line_end (&line_end);
  text = gtk_text_iter_get_slice (iter, &line_end);

  gtk_text_iter_set_line_offset (iter, (int) palang_lsp_chars_for_utf16 (text, character));
}
