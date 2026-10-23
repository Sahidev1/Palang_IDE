/* palang-complaints.c — the complaints file (see the header). */

#include "config.h"

#include <string.h>

#include "palang-complaints.h"

/* Written once, at the top of a new complaints file. */
static const char file_heading[] =
  "# Complaints about Palang\n"
  "\n"
  "Written from Palang's complaint box (Ctrl+Shift+F1, or\n"
  "`palang complain …` in Palang's terminal), oldest first. Claude reads\n"
  "this at the start of each session.\n";

GFile *
palang_complaints_get_file (const char *path)
{
  g_autofree char *full_path = NULL;

  if (path == NULL || path[0] == '\0')
    full_path = g_build_filename (g_get_user_data_dir (), "palang", "complaints.md", NULL);
  else if (g_str_has_prefix (path, "~/"))
    full_path = g_build_filename (g_get_home_dir (), path + 2, NULL);
  else if (!g_path_is_absolute (path))
    full_path = g_build_filename (g_get_home_dir (), path, NULL);
  else
    full_path = g_strdup (path);

  return g_file_new_for_path (full_path);
}

char *
palang_complaints_format_entry (GDateTime  *time,
                                const char *context,
                                const char *text)
{
  g_autoptr (GDateTime) local_time = g_date_time_to_local (time);
  g_autofree char *when = g_date_time_format (local_time, "%Y-%m-%d %H:%M");
  /* g_strstrip works in place, so strip a copy. */
  g_autofree char *body = g_strstrip (g_strdup (text));

  /* The blank line first keeps the heading on its own line even if the
   * file was edited by hand and doesn't end with one. */
  if (context != NULL && context[0] != '\0')
    return g_strdup_printf ("\n## %s · %s\n\n%s\n", when, context, body);

  return g_strdup_printf ("\n## %s\n\n%s\n", when, body);
}

static gboolean
write_text (GOutputStream  *stream,
            const char     *text,
            GError        **error)
{
  /* write_all keeps writing until everything is written (a single write
   * may write only part). */
  return g_output_stream_write_all (stream, text, strlen (text), NULL, NULL, error);
}

gboolean
palang_complaints_append (GFile       *file,
                          const char  *entry,
                          GError     **error)
{
  g_autoptr (GFile) folder = g_file_get_parent (file);
  g_autoptr (GFileOutputStream) stream = NULL;
  g_autoptr (GError) local_error = NULL;
  gboolean is_new;

  g_return_val_if_fail (G_IS_FILE (file), FALSE);
  g_return_val_if_fail (entry != NULL, FALSE);

  if (folder != NULL &&
      !g_file_make_directory_with_parents (folder, NULL, &local_error) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  g_clear_error (&local_error);

  /* Try to create the file; if it already exists, add to its end instead.
   * Doing it in this order tells us whether the heading is needed. */
  stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &local_error);
  is_new = (stream != NULL);
  if (stream == NULL)
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      stream = g_file_append_to (file, G_FILE_CREATE_NONE, NULL, error);
      if (stream == NULL)
        return FALSE;
    }

  if (is_new && !write_text (G_OUTPUT_STREAM (stream), file_heading, error))
    return FALSE;

  if (!write_text (G_OUTPUT_STREAM (stream), entry, error))
    return FALSE;

  return g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, error);
}
