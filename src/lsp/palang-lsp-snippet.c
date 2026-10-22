/* palang-lsp-snippet.c — reading the servers' snippets (see the header
 * for the syntax).
 *
 * A small recursive-descent parser: `p` points at the next character to
 * read. parse_sequence () reads text until a stop character (the "}"
 * closing a placeholder, or the end), handing every "$" to parse_dollar ().
 * Anything that turns out not to be valid syntax is kept as plain text,
 * so a server's odd snippet still inserts something sensible.
 */

#include "config.h"

#include <string.h>

#include "palang-lsp-snippet.h"

static void
part_free (PalangLspSnippetPart *part)
{
  g_free (part->text);
  g_free (part);
}

/* Appends @length bytes of @text as plain text, joining it to the last
 * part if that is plain text too. */
static void
add_text (GPtrArray  *parts,
          const char *text,
          gsize       length)
{
  PalangLspSnippetPart *last = parts->len > 0 ? g_ptr_array_index (parts, parts->len - 1) : NULL;
  PalangLspSnippetPart *part;

  if (last != NULL && last->tab_stop == -1)
    {
      char *joined = g_strdup_printf ("%s%.*s", last->text, (int) length, text);

      g_free (last->text);
      last->text = joined;
      return;
    }

  part = g_new0 (PalangLspSnippetPart, 1);
  part->tab_stop = -1;
  part->text = g_strndup (text, length);
  g_ptr_array_add (parts, part);
}

static void
add_tab_stop (GPtrArray  *parts,
              int         tab_stop,
              const char *text)
{
  PalangLspSnippetPart *part = g_new0 (PalangLspSnippetPart, 1);

  part->tab_stop = tab_stop;
  part->text = g_strdup (text);
  g_ptr_array_add (parts, part);
}

/* Returns: (transfer full): the text of all @parts joined. */
static char *
join_text (GPtrArray *parts)
{
  GString *text = g_string_new (NULL);

  for (guint i = 0; i < parts->len; i++)
    g_string_append (text, ((PalangLspSnippetPart *) g_ptr_array_index (parts, i))->text);

  return g_string_free (text, FALSE);
}

/* Reads a number at *p. Returns: it, or -1 if there is none. */
static int
read_number (const char **p)
{
  int number = 0;

  if (!g_ascii_isdigit (**p))
    return -1;

  while (g_ascii_isdigit (**p))
    {
      number = MIN (number * 10 + (**p - '0'), 100000);  /* no overflow */
      (*p)++;
    }

  return number;
}

/* Reads a variable name at *p. Returns: TRUE if there was one. */
static gboolean
skip_name (const char **p)
{
  if (!g_ascii_isalpha (**p) && **p != '_')
    return FALSE;

  while (g_ascii_isalnum (**p) || **p == '_')
    (*p)++;

  return TRUE;
}

static void parse_sequence (const char **p, char stop, GPtrArray *parts);

/* Reads "…}" (a placeholder's text, which may hold more snippet syntax)
 * and returns its plain text, or NULL (with *p unchanged) if the "}" is
 * missing. */
static char *
read_placeholder_text (const char **p)
{
  g_autoptr (GPtrArray) inner = g_ptr_array_new_with_free_func ((GDestroyNotify) part_free);
  const char *start = *p;

  parse_sequence (p, '}', inner);
  if (**p != '}')
    {
      *p = start;
      return NULL;
    }

  (*p)++;
  return join_text (inner);
}

/* Reads "one,two|}" (after "${1|") and returns the first choice, or NULL
 * (with *p unchanged) if it doesn't end in "|}". */
static char *
read_first_choice (const char **p)
{
  GString *choice = g_string_new (NULL);
  const char *start = *p;
  gboolean first = TRUE;

  while (**p != '\0' && !(**p == '|' && (*p)[1] == '}'))
    {
      char c = **p;

      if (c == ',')
        {
          first = FALSE;  /* only the first choice is kept */
          (*p)++;
          continue;
        }

      /* "\," is a comma inside a choice, "\|" a bar, and so on. */
      if (c == '\\' && (*p)[1] != '\0' && strchr ("$}\\,|", (*p)[1]) != NULL)
        {
          (*p)++;
          c = **p;
        }

      if (first)
        g_string_append_c (choice, c);
      (*p)++;
    }

  if (**p == '\0')
    {
      *p = start;
      g_string_free (choice, TRUE);
      return NULL;
    }

  *p += 2;  /* "|}" */
  return g_string_free (choice, FALSE);
}

/* *p is at a "$": reads a tab stop, placeholder, choice or variable. If
 * what follows isn't one, the "$" is plain text. */
static void
parse_dollar (const char **p,
              GPtrArray   *parts)
{
  const char *start = *p;
  int tab_stop;

  (*p)++;  /* "$" */

  /* $1 */
  tab_stop = read_number (p);
  if (tab_stop >= 0)
    {
      add_tab_stop (parts, tab_stop, "");
      return;
    }

  /* $NAME: a variable we don't know; it inserts nothing. */
  if (skip_name (p))
    return;

  if (**p == '{')
    {
      g_autofree char *text = NULL;

      (*p)++;
      tab_stop = read_number (p);

      if (tab_stop >= 0 && **p == '}')                      /* ${1} */
        {
          (*p)++;
          add_tab_stop (parts, tab_stop, "");
          return;
        }

      if (tab_stop >= 0 && **p == ':')                      /* ${1:text} */
        {
          (*p)++;
          text = read_placeholder_text (p);
          if (text != NULL)
            {
              add_tab_stop (parts, tab_stop, text);
              return;
            }
        }
      else if (tab_stop >= 0 && **p == '|')                 /* ${1|a,b|} */
        {
          (*p)++;
          text = read_first_choice (p);
          if (text != NULL)
            {
              add_tab_stop (parts, tab_stop, text);
              return;
            }
        }
      else if (tab_stop < 0 && skip_name (p))
        {
          if (**p == '}')                                   /* ${NAME} */
            {
              (*p)++;
              return;
            }
          if (**p == ':')                                   /* ${NAME:default} */
            {
              (*p)++;
              text = read_placeholder_text (p);
              if (text != NULL)
                {
                  add_text (parts, text, strlen (text));
                  return;
                }
            }
        }
    }

  /* Not snippet syntax: the "$" is just a dollar sign. */
  *p = start + 1;
  add_text (parts, "$", 1);
}

/* Reads until @stop (not consumed) or the end of the text. */
static void
parse_sequence (const char **p,
                char         stop,
                GPtrArray   *parts)
{
  while (**p != '\0' && **p != stop)
    {
      if (**p == '\\' && (*p)[1] != '\0' && strchr ("$}\\", (*p)[1]) != NULL)
        {
          add_text (parts, *p + 1, 1);  /* an escaped character */
          *p += 2;
        }
      else if (**p == '$')
        {
          parse_dollar (p, parts);
        }
      else
        {
          /* Plain text up to the next special character, in one piece. */
          gsize length = strcspn (*p, stop != '\0' ? "\\$}" : "\\$");

          if (length == 0)
            length = 1;  /* a backslash before an ordinary character */
          add_text (parts, *p, length);
          *p += length;
        }
    }
}

GPtrArray *
palang_lsp_snippet_parse (const char *snippet)
{
  GPtrArray *parts = g_ptr_array_new_with_free_func ((GDestroyNotify) part_free);
  const char *p = snippet;

  g_return_val_if_fail (snippet != NULL, parts);

  parse_sequence (&p, '\0', parts);
  return parts;
}

char *
palang_lsp_snippet_to_text (const char *snippet)
{
  g_autoptr (GPtrArray) parts = palang_lsp_snippet_parse (snippet);

  return join_text (parts);
}
