/* palang-rg.c — ripgrep's arguments and JSON output (see the header).
 *
 * A "match" line from `rg --json` looks like this (shortened):
 *
 *   {"type":"match","data":{
 *      "path":{"text":"src/main.c"},
 *      "lines":{"text":"  return run (app);\n"},
 *      "line_number":12,
 *      "submatches":[{"match":{"text":"run"},"start":9,"end":12}]}}
 *
 * Two details matter:
 *   - "start" and "end" count bytes, while the editor counts characters
 *     ("ö" is two bytes but one character).
 *   - Text that isn't valid UTF-8 (a Latin-1 file, an odd file name) comes
 *     as {"bytes":"<base64>"} instead of {"text":"…"}.
 */

#include "config.h"

#include <string.h>

#include <json-glib/json-glib.h>

#include "palang-rg.h"

char **
palang_rg_build_argv (const char        *text,
                      PalangSearchFlags  flags)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

  g_return_val_if_fail (text != NULL, NULL);

  g_strv_builder_add_many (builder, "rg", "--json", NULL);

  /* Skip files rg can't read instead of complaining about each one. */
  g_strv_builder_add (builder, "--no-messages");

  /* Files in alphabetical order, so results (and which ones are left out
   * past the limit) are the same every time. It makes rg use one thread,
   * still quick for a project. */
  g_strv_builder_add_many (builder, "--sort", "path", NULL);

  if (!(flags & PALANG_SEARCH_MATCH_CASE))
    g_strv_builder_add (builder, "--ignore-case");
  if (flags & PALANG_SEARCH_WHOLE_WORD)
    g_strv_builder_add (builder, "--word-regexp");
  if (!(flags & PALANG_SEARCH_REGEX))
    g_strv_builder_add (builder, "--fixed-strings");

  /* -e: the next word is the pattern, even if it starts with "-". */
  g_strv_builder_add_many (builder, "-e", text, NULL);

  return g_strv_builder_end (builder);
}

/* Returns: (transfer full) (nullable): the raw bytes of a {"text": …} or
 * {"bytes": …} object, NUL-terminated; @length is set without the NUL. */
static char *
get_raw_data (JsonObject *object,
              gsize      *length)
{
  if (object == NULL)
    return NULL;

  if (json_object_has_member (object, "text"))
    {
      const char *text = json_object_get_string_member (object, "text");

      if (text == NULL)
        return NULL;
      *length = strlen (text);
      return g_strdup (text);
    }

  if (json_object_has_member (object, "bytes"))
    {
      const char *base64 = json_object_get_string_member (object, "bytes");
      guchar *decoded;
      char *raw;

      if (base64 == NULL)
        return NULL;
      decoded = g_base64_decode (base64, length);
      raw = g_malloc (*length + 1);
      memcpy (raw, decoded, *length);
      raw[*length] = '\0';
      g_free (decoded);
      return raw;
    }

  return NULL;
}

/* The character offset of byte @byte_offset in @raw (which may contain
 * invalid UTF-8): count the characters of everything before it, after
 * making that part valid the same way the whole line is made valid. */
static guint
char_offset (const char *raw,
             gsize       length,
             gint64      byte_offset)
{
  g_autofree char *before = NULL;

  byte_offset = CLAMP (byte_offset, 0, (gint64) length);
  before = g_utf8_make_valid (raw, byte_offset);
  return g_utf8_strlen (before, -1);
}

static void
set_format_error (GError     **error,
                  const char  *line)
{
  g_set_error (error, JSON_PARSER_ERROR, JSON_PARSER_ERROR_INVALID_DATA,
               "Unexpected output from rg: %.80s", line);
}

PalangSearchMatch *
palang_rg_parse_line (const char  *line,
                      GFile       *folder,
                      GError     **error)
{
  g_autoptr (JsonParser) parser = json_parser_new ();
  g_autofree char *raw_path = NULL;
  g_autofree char *raw_text = NULL;
  g_autofree char *display_path = NULL;
  g_autofree char *text = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GArray) ranges = NULL;
  JsonObject *root;
  JsonObject *data;
  JsonArray *submatches;
  gsize path_length;
  gsize text_length;
  gint64 line_number;

  g_return_val_if_fail (line != NULL, NULL);
  g_return_val_if_fail (G_IS_FILE (folder), NULL);

  if (!json_parser_load_from_data (parser, line, -1, error))
    return NULL;

  if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
    {
      set_format_error (error, line);
      return NULL;
    }
  root = json_node_get_object (json_parser_get_root (parser));

  /* "begin", "end" and "summary" lines carry nothing we show. */
  if (g_strcmp0 (json_object_get_string_member_with_default (root, "type", ""), "match") != 0)
    return NULL;

  data = json_object_get_object_member (root, "data");
  if (data == NULL ||
      !json_object_has_member (data, "submatches") ||
      !json_object_has_member (data, "path") ||
      !json_object_has_member (data, "lines"))
    {
      set_format_error (error, line);
      return NULL;
    }

  raw_path = get_raw_data (json_object_get_object_member (data, "path"), &path_length);
  raw_text = get_raw_data (json_object_get_object_member (data, "lines"), &text_length);
  submatches = json_object_get_array_member (data, "submatches");
  line_number = json_object_get_int_member_with_default (data, "line_number", 0);

  if (raw_path == NULL || raw_text == NULL || submatches == NULL ||
      json_array_get_length (submatches) == 0 || line_number <= 0)
    {
      set_format_error (error, line);
      return NULL;
    }

  /* rg prints paths relative to the folder it runs in. A file name is just
   * bytes on Linux; g_filename_display_name () makes a readable version. */
  file = g_file_resolve_relative_path (folder, raw_path);
  display_path = g_filename_display_name (raw_path);

  /* The line without its line break, as valid UTF-8. */
  while (text_length > 0 && (raw_text[text_length - 1] == '\n' || raw_text[text_length - 1] == '\r'))
    text_length--;
  text = g_utf8_make_valid (raw_text, text_length);

  ranges = g_array_new (FALSE, FALSE, sizeof (guint));
  for (guint i = 0; i < json_array_get_length (submatches); i++)
    {
      JsonObject *submatch = json_array_get_object_element (submatches, i);
      guint start;
      guint end;

      if (submatch == NULL)
        continue;

      start = char_offset (raw_text, text_length, json_object_get_int_member_with_default (submatch, "start", 0));
      end = char_offset (raw_text, text_length, json_object_get_int_member_with_default (submatch, "end", 0));
      g_array_append_val (ranges, start);
      g_array_append_val (ranges, end);
    }

  if (ranges->len == 0)
    {
      set_format_error (error, line);
      return NULL;
    }

  return palang_search_match_new (file, display_path, (guint) line_number, text,
                                  (const guint *) ranges->data, ranges->len / 2);
}
