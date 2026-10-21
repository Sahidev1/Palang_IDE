/* palang-session.c — read and write session.ini (see the header). */

#include "config.h"

#include "palang-session.h"

#define GROUP "session"

PalangSession *
palang_session_new (void)
{
  PalangSession *session = g_new0 (PalangSession, 1);

  session->files = g_ptr_array_new_with_free_func (g_object_unref);
  session->active = -1;
  return session;
}

void
palang_session_free (PalangSession *session)
{
  if (session == NULL)
    return;

  g_clear_object (&session->folder);
  g_free (session->panel_page);
  g_ptr_array_unref (session->files);
  g_free (session);
}

char *
palang_session_default_path (void)
{
  return g_build_filename (g_get_user_state_dir (), "palang", "session.ini", NULL);
}

gboolean
palang_session_save (const PalangSession  *session,
                     const char           *path,
                     GError              **error)
{
  g_autoptr (GKeyFile) key_file = g_key_file_new ();
  g_autoptr (GStrvBuilder) uris = g_strv_builder_new ();
  g_auto (GStrv) uri_list = NULL;
  g_autofree char *directory = g_path_get_dirname (path);

  g_return_val_if_fail (session != NULL, FALSE);
  g_return_val_if_fail (path != NULL, FALSE);

  if (session->folder != NULL)
    {
      g_autofree char *folder_uri = g_file_get_uri (session->folder);
      g_key_file_set_string (key_file, GROUP, "folder", folder_uri);
    }

  for (guint i = 0; i < session->files->len; i++)
    g_strv_builder_take (uris, g_file_get_uri (g_ptr_array_index (session->files, i)));
  uri_list = g_strv_builder_end (uris);

  g_key_file_set_string_list (key_file, GROUP, "files",
                              (const char * const *) uri_list, g_strv_length (uri_list));
  g_key_file_set_integer (key_file, GROUP, "active", session->active);
  g_key_file_set_boolean (key_file, GROUP, "show-sidebar", session->show_sidebar);
  g_key_file_set_boolean (key_file, GROUP, "show-panel", session->show_panel);
  g_key_file_set_integer (key_file, GROUP, "panel-height", session->panel_height);
  if (session->panel_page != NULL)
    g_key_file_set_string (key_file, GROUP, "panel-page", session->panel_page);

  g_mkdir_with_parents (directory, 0700);
  return g_key_file_save_to_file (key_file, path, error);
}

PalangSession *
palang_session_load (const char *path)
{
  g_autoptr (GKeyFile) key_file = g_key_file_new ();
  g_autoptr (PalangSession) session = NULL;
  g_autofree char *folder_uri = NULL;
  g_auto (GStrv) uris = NULL;

  g_return_val_if_fail (path != NULL, NULL);

  if (!g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, NULL))
    return NULL;

  session = palang_session_new ();

  /* Each key is optional; a missing or broken one keeps its default. */
  folder_uri = g_key_file_get_string (key_file, GROUP, "folder", NULL);
  if (folder_uri != NULL && folder_uri[0] != '\0')
    session->folder = g_file_new_for_uri (folder_uri);

  uris = g_key_file_get_string_list (key_file, GROUP, "files", NULL, NULL);
  for (guint i = 0; uris != NULL && uris[i] != NULL; i++)
    if (uris[i][0] != '\0')
      g_ptr_array_add (session->files, g_file_new_for_uri (uris[i]));

  session->active = g_key_file_get_integer (key_file, GROUP, "active", NULL);
  if (session->active < 0 || session->active >= (int) session->files->len)
    session->active = session->files->len > 0 ? 0 : -1;

  session->show_sidebar = g_key_file_get_boolean (key_file, GROUP, "show-sidebar", NULL);
  session->show_panel = g_key_file_get_boolean (key_file, GROUP, "show-panel", NULL);

  session->panel_height = g_key_file_get_integer (key_file, GROUP, "panel-height", NULL);
  if (session->panel_height < 0)
    session->panel_height = 0;

  session->panel_page = g_key_file_get_string (key_file, GROUP, "panel-page", NULL);

  return g_steal_pointer (&session);
}
