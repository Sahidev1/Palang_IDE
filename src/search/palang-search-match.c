/* palang-search-match.c — one line found by a project search (see the
 * header). A value object: everything is set at creation.
 */

#include "config.h"

#include "palang-search-match.h"

/* Lines longer than this (minified files, data) are cut off in the list. */
#define MAX_SHOWN_CHARS 300

struct _PalangSearchMatch
{
  GObject parent_instance;

  GFile  *file;
  char   *path;
  guint   line;
  char   *text;
  guint  *ranges;    /* n_ranges pairs: start, end */
  guint   n_ranges;
};

G_DEFINE_FINAL_TYPE (PalangSearchMatch, palang_search_match, G_TYPE_OBJECT)

GFile *
palang_search_match_get_file (PalangSearchMatch *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_MATCH (self), NULL);

  return self->file;
}

const char *
palang_search_match_get_path (PalangSearchMatch *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_MATCH (self), NULL);

  return self->path;
}

guint
palang_search_match_get_line (PalangSearchMatch *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_MATCH (self), 0);

  return self->line;
}

guint
palang_search_match_get_column (PalangSearchMatch *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_MATCH (self), 0);

  return self->ranges[0] + 1;
}

guint
palang_search_match_get_length (PalangSearchMatch *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_MATCH (self), 0);

  return self->ranges[1] - self->ranges[0];
}

const char *
palang_search_match_get_text (PalangSearchMatch *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_MATCH (self), NULL);

  return self->text;
}

/* Appends the characters [@start, @end) of @text, escaped for markup.
 * Offsets are in characters; g_utf8_offset_to_pointer finds their bytes. */
static void
append_escaped (GString    *markup,
                const char *text,
                guint       start,
                guint       end)
{
  const char *from = g_utf8_offset_to_pointer (text, start);
  const char *to = g_utf8_offset_to_pointer (text, end);
  g_autofree char *escaped = g_markup_escape_text (from, to - from);

  g_string_append (markup, escaped);
}

char *
palang_search_match_dup_markup (PalangSearchMatch *self)
{
  GString *markup;
  guint length;
  guint position = 0;
  guint shown_end;

  g_return_val_if_fail (PALANG_IS_SEARCH_MATCH (self), NULL);

  markup = g_string_new (NULL);
  length = g_utf8_strlen (self->text, -1);

  /* Skip the indentation, but never past the first match. */
  while (position < self->ranges[0] &&
         g_unichar_isspace (g_utf8_get_char (g_utf8_offset_to_pointer (self->text, position))))
    position++;

  /* A match far into a long line: start a little before it instead. */
  if (self->ranges[0] > position + MAX_SHOWN_CHARS / 2)
    {
      position = self->ranges[0] - 20;
      g_string_append (markup, "…");
    }

  shown_end = MIN (length, position + MAX_SHOWN_CHARS);

  for (guint i = 0; i < self->n_ranges; i++)
    {
      guint start = MIN (self->ranges[2 * i], shown_end);
      guint end = MIN (self->ranges[2 * i + 1], shown_end);

      append_escaped (markup, self->text, position, start);
      g_string_append (markup, "<b>");
      append_escaped (markup, self->text, start, end);
      g_string_append (markup, "</b>");
      position = end;
    }

  append_escaped (markup, self->text, position, shown_end);
  if (shown_end < length)
    g_string_append (markup, "…");

  return g_string_free (markup, FALSE);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_search_match_finalize (GObject *object)
{
  PalangSearchMatch *self = PALANG_SEARCH_MATCH (object);

  g_clear_object (&self->file);
  g_free (self->path);
  g_free (self->text);
  g_free (self->ranges);

  G_OBJECT_CLASS (palang_search_match_parent_class)->finalize (object);
}

static void
palang_search_match_class_init (PalangSearchMatchClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = palang_search_match_finalize;
}

static void
palang_search_match_init (PalangSearchMatch *self)
{
}

PalangSearchMatch *
palang_search_match_new (GFile       *file,
                         const char  *path,
                         guint        line,
                         const char  *text,
                         const guint *ranges,
                         guint        n_ranges)
{
  PalangSearchMatch *self;

  g_return_val_if_fail (G_IS_FILE (file), NULL);
  g_return_val_if_fail (path != NULL && text != NULL, NULL);
  g_return_val_if_fail (ranges != NULL && n_ranges > 0, NULL);

  self = g_object_new (PALANG_TYPE_SEARCH_MATCH, NULL);
  self->file = g_object_ref (file);
  self->path = g_strdup (path);
  self->line = line;
  self->text = g_strdup (text);
  self->ranges = g_memdup2 (ranges, 2 * n_ranges * sizeof (guint));
  self->n_ranges = n_ranges;

  return self;
}
