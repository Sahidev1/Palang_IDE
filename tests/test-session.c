/* test-session.c — saving and loading session.ini. No display needed. */

#include "config.h"

#include <glib/gstdio.h>

#include "palang-session.h"

typedef struct {
  char *dir;
  char *path;
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", NULL);
  fixture->path = g_build_filename (fixture->dir, "session.ini", NULL);
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_unlink (fixture->path);
  g_rmdir (fixture->dir);
  g_free (fixture->path);
  g_free (fixture->dir);
}

static void
test_round_trip (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSession) saved = palang_session_new ();
  g_autoptr (PalangSession) loaded = NULL;
  g_autoptr (GError) error = NULL;
  /* Unusual characters must survive (stored as URIs). */
  const char *paths[] = { "/tmp/a.c", "/tmp/with space/ö;semi=colon.txt" };

  saved->folder = g_file_new_for_path ("/tmp/project");
  for (guint i = 0; i < G_N_ELEMENTS (paths); i++)
    g_ptr_array_add (saved->files, g_file_new_for_path (paths[i]));
  saved->active = 1;
  saved->show_sidebar = TRUE;

  g_assert_true (palang_session_save (saved, fixture->path, &error));
  g_assert_no_error (error);

  loaded = palang_session_load (fixture->path);
  g_assert_nonnull (loaded);
  g_assert_true (g_file_equal (loaded->folder, saved->folder));
  g_assert_cmpuint (loaded->files->len, ==, 2);
  for (guint i = 0; i < G_N_ELEMENTS (paths); i++)
    {
      g_autofree char *path = g_file_get_path (g_ptr_array_index (loaded->files, i));
      g_assert_cmpstr (path, ==, paths[i]);
    }
  g_assert_cmpint (loaded->active, ==, 1);
  g_assert_true (loaded->show_sidebar);
}

static void
test_empty_session (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSession) saved = palang_session_new ();
  g_autoptr (PalangSession) loaded = NULL;

  g_assert_true (palang_session_save (saved, fixture->path, NULL));
  loaded = palang_session_load (fixture->path);

  g_assert_nonnull (loaded);
  g_assert_null (loaded->folder);
  g_assert_cmpuint (loaded->files->len, ==, 0);
  g_assert_cmpint (loaded->active, ==, -1);
}

static void
test_missing_file (Fixture *fixture, gconstpointer data)
{
  g_assert_null (palang_session_load (fixture->path));
}

/* A broken "active" index is corrected instead of trusted. */
static void
test_bad_active_index (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSession) loaded = NULL;

  g_file_set_contents (fixture->path,
                       "[session]\n"
                       "files=file:///tmp/a.c;\n"
                       "active=7\n", -1, NULL);
  loaded = palang_session_load (fixture->path);

  g_assert_nonnull (loaded);
  g_assert_cmpint (loaded->active, ==, 0);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD_TEST ("/session/round-trip", test_round_trip);
  ADD_TEST ("/session/empty", test_empty_session);
  ADD_TEST ("/session/missing-file", test_missing_file);
  ADD_TEST ("/session/bad-active-index", test_bad_active_index);

#undef ADD_TEST

  return g_test_run ();
}
