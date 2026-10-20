/* test-file-tree.c — tests for the sidebar's folder tree.
 *
 * Each test builds a small folder structure in a temporary directory and
 * checks the rows the tree shows, written as one string such as
 * "a_dir, b_dir, file2.txt" (children of an expanded folder are indented
 * with "  "). The tree loads folders in the background, so tests wait until
 * the expected rows appear.
 */

#include "config.h"

#include <glib/gstdio.h>

#include "palang-file-tree.h"

/* ---- Helpers ------------------------------------------------------------ */

/* Returns: (transfer full): the visible rows as "name, name, ...". */
static char *
dup_rows (PalangFileTree *tree)
{
  GListModel *rows = palang_file_tree_get_rows (tree);
  g_autoptr (GString) text = g_string_new (NULL);
  guint n = rows != NULL ? g_list_model_get_n_items (rows) : 0;

  for (guint i = 0; i < n; i++)
    {
      g_autoptr (GtkTreeListRow) row = g_list_model_get_item (rows, i);
      g_autofree char *name = g_file_get_basename (palang_file_tree_row_get_file (row));

      if (i > 0)
        g_string_append (text, ", ");
      for (guint depth = 0; depth < gtk_tree_list_row_get_depth (row); depth++)
        g_string_append (text, "  ");
      g_string_append (text, name);
    }

  return g_string_free (g_steal_pointer (&text), FALSE);
}

/* Runs the main loop until the tree shows exactly @expected, failing (and
 * printing what it shows instead) after 5 seconds. */
static void
wait_for_rows (PalangFileTree *tree,
               const char     *expected)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  for (;;)
    {
      g_autofree char *rows = dup_rows (tree);

      if (g_strcmp0 (rows, expected) == 0)
        return;

      if (g_get_monotonic_time () > deadline)
        g_error ("Expected rows \"%s\" but got \"%s\"", expected, rows);

      g_main_context_iteration (NULL, FALSE);
    }
}

/* Returns: (transfer none): the visible row showing @name. */
static GtkTreeListRow *
find_row (PalangFileTree *tree,
          const char     *name)
{
  GListModel *rows = palang_file_tree_get_rows (tree);

  for (guint i = 0; i < g_list_model_get_n_items (rows); i++)
    {
      g_autoptr (GtkTreeListRow) row = g_list_model_get_item (rows, i);
      g_autofree char *basename = g_file_get_basename (palang_file_tree_row_get_file (row));

      if (g_strcmp0 (basename, name) == 0)
        return row;  /* the model still holds a reference */
    }

  g_error ("No row named %s", name);
}

typedef struct {
  char           *dir;
  PalangFileTree *tree;
} Fixture;

static void
make_file (Fixture *fixture, const char *relative_path)
{
  g_autofree char *path = g_build_filename (fixture->dir, relative_path, NULL);
  g_autoptr (GError) error = NULL;

  g_file_set_contents (path, "", 0, &error);
  g_assert_no_error (error);
}

static void
make_dir (Fixture *fixture, const char *relative_path)
{
  g_autofree char *path = g_build_filename (fixture->dir, relative_path, NULL);

  g_assert_cmpint (g_mkdir (path, 0700), ==, 0);
}

/* Builds this structure (names chosen to check the sort order):
 *
 *   b_dir/inner.txt   a_dir/   file10.txt   file2.txt   zeta.c   .hidden
 */
static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) root = NULL;

  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", &error);
  g_assert_no_error (error);

  make_dir (fixture, "b_dir");
  make_file (fixture, "b_dir/inner.txt");
  make_dir (fixture, "a_dir");
  make_file (fixture, "file10.txt");
  make_file (fixture, "file2.txt");
  make_file (fixture, "zeta.c");
  make_file (fixture, ".hidden");

  fixture->tree = g_object_ref_sink (palang_file_tree_new ());
  root = g_file_new_for_path (fixture->dir);
  palang_file_tree_set_root (fixture->tree, root);
}

/* Recursively deletes @path. */
static void
remove_all (const char *path)
{
  g_autoptr (GDir) dir = g_dir_open (path, 0, NULL);
  const char *name;

  while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *child = g_build_filename (path, name, NULL);

      if (g_file_test (child, G_FILE_TEST_IS_DIR))
        remove_all (child);
      else
        g_unlink (child);
    }
  g_rmdir (path);
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_clear_object (&fixture->tree);
  remove_all (fixture->dir);
  g_free (fixture->dir);
}

/* ---- Tests -------------------------------------------------------------- */

/* Folders first, then files in natural order (file2 before file10); hidden
 * files are left out. */
static void
test_sorted_without_hidden (Fixture *fixture, gconstpointer data)
{
  wait_for_rows (fixture->tree, "a_dir, b_dir, file2.txt, file10.txt, zeta.c");
}

static void
test_show_hidden (Fixture *fixture, gconstpointer data)
{
  wait_for_rows (fixture->tree, "a_dir, b_dir, file2.txt, file10.txt, zeta.c");

  palang_file_tree_set_show_hidden (fixture->tree, TRUE);
  wait_for_rows (fixture->tree, "a_dir, b_dir, .hidden, file2.txt, file10.txt, zeta.c");

  palang_file_tree_set_show_hidden (fixture->tree, FALSE);
  wait_for_rows (fixture->tree, "a_dir, b_dir, file2.txt, file10.txt, zeta.c");
}

static void
test_expand_folder (Fixture *fixture, gconstpointer data)
{
  wait_for_rows (fixture->tree, "a_dir, b_dir, file2.txt, file10.txt, zeta.c");

  gtk_tree_list_row_set_expanded (find_row (fixture->tree, "b_dir"), TRUE);
  wait_for_rows (fixture->tree, "a_dir, b_dir,   inner.txt, file2.txt, file10.txt, zeta.c");
}

/* The tree follows changes on disk without being told. */
static void
test_follows_disk_changes (Fixture *fixture, gconstpointer data)
{
  g_autofree char *path = g_build_filename (fixture->dir, "file2.txt", NULL);

  wait_for_rows (fixture->tree, "a_dir, b_dir, file2.txt, file10.txt, zeta.c");

  make_file (fixture, "main.c");
  wait_for_rows (fixture->tree, "a_dir, b_dir, file2.txt, file10.txt, main.c, zeta.c");

  g_unlink (path);
  wait_for_rows (fixture->tree, "a_dir, b_dir, file10.txt, main.c, zeta.c");
}

/* Renames (`mv a b`) and files moved in from elsewhere show up too. */
static void
test_follows_renames_and_moves (Fixture *fixture, gconstpointer data)
{
  g_autofree char *old_path = g_build_filename (fixture->dir, "zeta.c", NULL);
  g_autofree char *new_path = g_build_filename (fixture->dir, "alpha.c", NULL);
  g_autofree char *outside_dir = g_dir_make_tmp ("palang-test-outside-XXXXXX", NULL);
  g_autofree char *outside = g_build_filename (outside_dir, "moved.txt", NULL);
  g_autofree char *inside = g_build_filename (fixture->dir, "moved.txt", NULL);

  wait_for_rows (fixture->tree, "a_dir, b_dir, file2.txt, file10.txt, zeta.c");

  g_assert_cmpint (g_rename (old_path, new_path), ==, 0);
  wait_for_rows (fixture->tree, "a_dir, b_dir, alpha.c, file2.txt, file10.txt");

  g_file_set_contents (outside, "", 0, NULL);
  g_assert_cmpint (g_rename (outside, inside), ==, 0);
  wait_for_rows (fixture->tree, "a_dir, b_dir, alpha.c, file2.txt, file10.txt, moved.txt");

  g_rmdir (outside_dir);
}

/* A change in one folder must not collapse an expanded folder. */
static void
test_expanded_folder_stays_expanded (Fixture *fixture, gconstpointer data)
{
  wait_for_rows (fixture->tree, "a_dir, b_dir, file2.txt, file10.txt, zeta.c");
  gtk_tree_list_row_set_expanded (find_row (fixture->tree, "b_dir"), TRUE);
  wait_for_rows (fixture->tree, "a_dir, b_dir,   inner.txt, file2.txt, file10.txt, zeta.c");

  make_file (fixture, "new.txt");
  make_file (fixture, "b_dir/inner2.txt");
  wait_for_rows (fixture->tree,
                 "a_dir, b_dir,   inner.txt,   inner2.txt, file2.txt, file10.txt, new.txt, zeta.c");
}

static void
test_no_root (void)
{
  g_autoptr (PalangFileTree) tree = g_object_ref_sink (palang_file_tree_new ());

  g_assert_null (palang_file_tree_get_root (tree));
  g_assert_null (palang_file_tree_get_rows (tree));
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  adw_init ();

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  g_test_add_func ("/file-tree/no-root", test_no_root);
  ADD_TEST ("/file-tree/sorted-without-hidden", test_sorted_without_hidden);
  ADD_TEST ("/file-tree/show-hidden", test_show_hidden);
  ADD_TEST ("/file-tree/expand-folder", test_expand_folder);
  ADD_TEST ("/file-tree/follows-disk-changes", test_follows_disk_changes);
  ADD_TEST ("/file-tree/follows-renames-and-moves", test_follows_renames_and_moves);
  ADD_TEST ("/file-tree/expanded-folder-stays-expanded", test_expanded_folder_stays_expanded);

#undef ADD_TEST

  return g_test_run ();
}
