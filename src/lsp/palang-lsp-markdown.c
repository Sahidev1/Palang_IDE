/* palang-lsp-markdown.c — the servers' Markdown as Pango markup (see the
 * header for what is supported).
 *
 * Works line by line. A line is either inside a code block (shown as it
 * is, in monospace) or ordinary text, where a heading or separator is
 * recognised by the whole line, and inline code, emphasis and escapes by
 * scanning the line from left to right (append_inline ()).
 *
 * Every piece of the server's text goes through g_markup_escape_text ()
 * ("<" → "&lt;"), and every tag we add is closed on the same line, so the
 * result is always valid markup: a "<" in a C++ template, or a "*" that
 * never closes, just shows as written.
 */

#include "config.h"

#include <string.h>

#include "palang-lsp-markdown.h"

/* Appends @length bytes of @text (-1: all of it) to @out, escaped for
 * markup. */
static void
append_escaped (GString    *out,
                const char *text,
                gssize      length)
{
  g_autofree char *escaped = g_markup_escape_text (text, length);

  g_string_append (out, escaped);
}

/* Markdown lets a backslash turn any ASCII punctuation character into
 * plain text ("\*" is a star, not emphasis). */
static gboolean
is_ascii_punctuation (char c)
{
  return g_ascii_ispunct (c);
}

/* Returns: how many times @c repeats at the start of @text. */
static gsize
run_length (const char *text,
            char        c)
{
  gsize n = 0;

  while (text[n] == c)
    n++;
  return n;
}

/* Returns: the index of the next run of exactly @n backticks in @text,
 * starting at @from, or -1: the end of an inline code span. */
static gssize
find_backticks (const char *text,
                gsize       from,
                gsize       n)
{
  gsize i = from;

  while (text[i] != '\0')
    {
      gsize run = run_length (text + i, '`');

      if (run == n)
        return i;
      i += run > 0 ? run : 1;
    }

  return -1;
}

/* Returns: the index of the run of exactly @n stars in @text that closes
 * emphasis opened just before @from, or -1. The closing stars must follow
 * a non-space ("a * b * c" is no emphasis) and something must come
 * between. Escaped stars ("\*") don't count. */
static gssize
find_closing_stars (const char *text,
                    gsize       from,
                    gsize       n)
{
  gsize i = from;

  while (text[i] != '\0')
    {
      gsize run;

      if (text[i] == '\\' && text[i + 1] != '\0')
        {
          i += 2;
          continue;
        }

      run = run_length (text + i, '*');
      if (run == n && i > from && text[i - 1] != ' ')
        return i;
      i += run > 0 ? run : 1;
    }

  return -1;
}

/* One line of ordinary text: escapes, `code`, *italic*, **bold**. */
static void
append_inline (GString    *out,
               const char *text)
{
  gsize i = 0;

  while (text[i] != '\0')
    {
      if (text[i] == '\\' && is_ascii_punctuation (text[i + 1]))
        {
          append_escaped (out, text + i + 1, 1);
          i += 2;
        }
      else if (text[i] == '`')
        {
          gsize n = run_length (text + i, '`');
          gssize end = find_backticks (text, i + n, n);

          if (end < 0)
            {
              append_escaped (out, text + i, n);  /* never closed: plain text */
              i += n;
              continue;
            }

          /* Inside code, backslashes and stars are plain text. */
          g_string_append (out, "<tt>");
          append_escaped (out, text + i + n, end - (i + n));
          g_string_append (out, "</tt>");
          i = end + n;
        }
      else if (text[i] == '*')
        {
          gsize n = run_length (text + i, '*');
          gssize end = -1;
          g_autofree char *inner = NULL;

          /* "*" opens italic and "**" bold if followed by a non-space and
           * closed later on the line. */
          if (n <= 2 && text[i + n] != ' ' && text[i + n] != '\0')
            end = find_closing_stars (text, i + n, n);

          if (end < 0)
            {
              append_escaped (out, text + i, n);
              i += n;
              continue;
            }

          /* The text between can itself hold code or escapes. */
          inner = g_strndup (text + i + n, end - (i + n));
          g_string_append (out, n == 2 ? "<b>" : "<i>");
          append_inline (out, inner);
          g_string_append (out, n == 2 ? "</b>" : "</i>");
          i = end + n;
        }
      else
        {
          /* Plain text up to the next special character, in one piece, so
           * multi-byte UTF-8 characters stay whole. */
          gsize length = strcspn (text + i, "\\`*");

          if (length == 0)
            length = 1;  /* a backslash before something that isn't punctuation */
          append_escaped (out, text + i, length);
          i += length;
        }
    }
}

/* Returns: (transfer none): @line without up to three leading spaces
 * (Markdown allows them before a fence, heading or separator). */
static const char *
skip_indent (const char *line)
{
  for (int i = 0; i < 3 && *line == ' '; i++)
    line++;
  return line;
}

/* "```c" or "~~~": the start or end of a code block. */
static gboolean
is_fence (const char *line)
{
  line = skip_indent (line);
  return g_str_has_prefix (line, "```") || g_str_has_prefix (line, "~~~");
}

/* "---", "***" or "___" (three or more, nothing else): a separator. */
static gboolean
is_separator (const char *line)
{
  line = skip_indent (line);
  if (*line != '-' && *line != '*' && *line != '_')
    return FALSE;
  return run_length (line, *line) >= 3 && line[run_length (line, *line)] == '\0';
}

/* Returns: (transfer none) (nullable): the title of a heading line ("###
 * Title" → "Title"), NULL if @line isn't one. */
static const char *
heading_title (const char *line)
{
  gsize hashes;

  line = skip_indent (line);
  hashes = run_length (line, '#');
  if (hashes < 1 || hashes > 6)
    return NULL;
  if (line[hashes] == '\0')
    return line + hashes;
  if (line[hashes] != ' ')
    return NULL;  /* "#include" is not a heading */
  return line + hashes + 1;
}

char *
palang_lsp_markdown_to_markup (const char *markdown)
{
  g_auto (GStrv) lines = NULL;
  g_autoptr (GPtrArray) converted = g_ptr_array_new_with_free_func (g_free);
  GString *out = g_string_new (NULL);
  gboolean in_code = FALSE;
  gboolean blank_pending = FALSE;

  g_return_val_if_fail (markdown != NULL, NULL);

  /* First convert each line on its own ("" for an empty line)... */
  lines = g_strsplit (markdown, "\n", -1);
  for (guint i = 0; lines[i] != NULL; i++)
    {
      char *line = g_strchomp (lines[i]);  /* also "\r" and trailing "  " */
      GString *markup;

      if (is_fence (line))
        {
          in_code = !in_code;
          continue;
        }

      markup = g_string_new (NULL);
      if (in_code)
        {
          if (line[0] != '\0')
            {
              g_string_append (markup, "<tt>");
              append_escaped (markup, line, -1);
              g_string_append (markup, "</tt>");
            }
        }
      else if (is_separator (line))
        ;  /* an empty line */
      else if (heading_title (line) != NULL)
        {
          g_string_append (markup, "<b>");
          append_inline (markup, heading_title (line));
          g_string_append (markup, "</b>");
        }
      else
        append_inline (markup, line);

      g_ptr_array_add (converted, g_string_free (markup, FALSE));
    }

  /* ...then join them, without empty lines at the start or end, and with
   * one empty line where there were several. */
  for (guint i = 0; i < converted->len; i++)
    {
      const char *markup = g_ptr_array_index (converted, i);

      if (markup[0] == '\0')
        {
          blank_pending = out->len > 0;
          continue;
        }

      if (out->len > 0)
        g_string_append (out, blank_pending ? "\n\n" : "\n");
      g_string_append (out, markup);
      blank_pending = FALSE;
    }

  return g_string_free (out, FALSE);
}

char *
palang_lsp_markdown_escape (const char *text)
{
  GString *out = g_string_new (NULL);

  g_return_val_if_fail (text != NULL, NULL);

  for (const char *p = text; *p != '\0'; p++)
    {
      if (is_ascii_punctuation (*p))
        g_string_append_c (out, '\\');
      g_string_append_c (out, *p);
    }

  return g_string_free (out, FALSE);
}
