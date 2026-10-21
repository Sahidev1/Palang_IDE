/* test-document.c — unit tests for PalangDocument (loading and saving).
 *
 * Each test works in its own temporary directory. Loading and saving are
 * async, so tests wait for them with wait_for_result(), which runs the GLib
 * main loop until the callback has fired.
 */

#include "config.h"

#include <string.h>

#include <glib/gstdio.h>

#include "palang-document.h"

/* ---- Helpers ------------------------------------------------------------ */

/* Async callback used by all tests: stores the result so the test can call
 * the matching _finish function. */
static void
store_result (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  GAsyncResult **out = user_data;

  *out = g_object_ref (result);
}

/* Runs the main loop until *result has been set by store_result(). */
static void
wait_for_result (GAsyncResult **result)
{
  while (*result == NULL)
    g_main_context_iteration (NULL, TRUE);
}

static gboolean
load (PalangDocument *document, GFile *file, GError **error)
{
  g_autoptr (GAsyncResult) result = NULL;

  palang_document_load_async (document, file, NULL, store_result, &result);
  wait_for_result (&result);
  return palang_document_load_finish (document, result, error);
}

static gboolean
save (PalangDocument *document, GFile *target, GError **error)
{
  g_autoptr (GAsyncResult) result = NULL;

  palang_document_save_async (document, target, FALSE, NULL, store_result, &result);
  wait_for_result (&result);
  return palang_document_save_finish (document, result, error);
}

static char *
get_text (PalangDocument *document)
{
  GtkTextIter start, end;

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (document), &start, &end);
  return gtk_text_buffer_get_text (GTK_TEXT_BUFFER (document), &start, &end, TRUE);
}

/* Per-test temporary directory, removed after the test. */
typedef struct {
  char *dir;
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GError) error = NULL;

  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", &error);
  g_assert_no_error (error);
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GDir) dir = g_dir_open (fixture->dir, 0, NULL);
  const char *name;

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *path = g_build_filename (fixture->dir, name, NULL);
      g_unlink (path);
    }
  g_rmdir (fixture->dir);
  g_free (fixture->dir);
}

/* Writes raw bytes to a file in the fixture directory. */
static GFile *
write_file (Fixture *fixture, const char *name, const char *contents, gssize length)
{
  g_autofree char *path = g_build_filename (fixture->dir, name, NULL);
  g_autoptr (GError) error = NULL;

  g_file_set_contents (path, contents, length, &error);
  g_assert_no_error (error);
  return g_file_new_for_path (path);
}

static char *
read_file (GFile *file, gsize *length)
{
  g_autoptr (GError) error = NULL;
  char *contents = NULL;

  g_file_load_contents (file, NULL, &contents, length, NULL, &error);
  g_assert_no_error (error);
  return contents;
}

/* ---- Tests -------------------------------------------------------------- */

static void
test_new_document (void)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autofree char *title = palang_document_dup_title (document);

  g_assert_null (palang_document_get_file (document));
  g_assert_cmpstr (title, ==, "Untitled");
}

static void
test_load (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autoptr (GFile) file = write_file (fixture, "hello.py", "print('hi')\n", -1);
  g_autoptr (GError) error = NULL;
  g_autofree char *text = NULL;
  g_autofree char *title = NULL;
  GtkSourceLanguage *language;

  g_assert_true (load (document, file, &error));
  g_assert_no_error (error);

  text = get_text (document);
  title = palang_document_dup_title (document);
  language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (document));

  g_assert_cmpstr (text, ==, "print('hi')\n");
  g_assert_cmpstr (title, ==, "hello.py");
  g_assert_true (g_file_equal (palang_document_get_file (document), file));
  g_assert_false (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (document)));
  g_assert_nonnull (language);
  g_assert_cmpstr (gtk_source_language_get_id (language), ==, "python3");
}

/* A failed load must not change the document's location or text. */
static void
test_load_missing_file (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autofree char *path = g_build_filename (fixture->dir, "missing.c", NULL);
  g_autoptr (GFile) missing = g_file_new_for_path (path);
  g_autoptr (GError) error = NULL;
  g_autofree char *text = NULL;

  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (document), "keep me", -1);

  g_assert_false (load (document, missing, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);

  text = get_text (document);
  g_assert_null (palang_document_get_file (document));
  g_assert_cmpstr (text, ==, "keep me");
}

/* Editing and saving must keep Windows (CRLF) line endings. */
static void
test_save_keeps_crlf (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autoptr (GFile) file = write_file (fixture, "dos.txt", "one\r\ntwo\r\n", -1);
  g_autoptr (GError) error = NULL;
  g_autofree char *on_disk = NULL;
  GtkTextIter end;

  g_assert_true (load (document, file, &error));

  gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (document), &end);
  gtk_text_buffer_insert (GTK_TEXT_BUFFER (document), &end, "three\n", -1);
  g_assert_true (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (document)));

  g_assert_true (save (document, NULL, &error));
  g_assert_no_error (error);
  g_assert_false (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (document)));

  on_disk = read_file (file, NULL);
  g_assert_cmpstr (on_disk, ==, "one\r\ntwo\r\nthree\r\n");
}

/* A Latin-1 file shows correctly and is saved back as Latin-1, not UTF-8.
 * @data is the file content (é is the single byte 0xE9 in ISO-8859-15). */
static void
test_save_keeps_encoding (Fixture *fixture, gconstpointer data)
{
  const char *latin1 = data;
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autoptr (GFile) file = write_file (fixture, "latin1.txt", latin1, -1);
  g_autoptr (GError) error = NULL;
  g_autofree char *expected_text = g_convert (latin1, -1, "UTF-8", "ISO-8859-15",
                                              NULL, NULL, NULL);
  g_autofree char *text = NULL;
  g_autofree char *on_disk = NULL;
  gsize length;

  g_assert_true (load (document, file, &error));
  g_assert_no_error (error);

  text = get_text (document);
  g_assert_cmpstr (text, ==, expected_text);

  /* Make it modified so the save really writes. */
  gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (document), TRUE);
  g_assert_true (save (document, NULL, &error));

  on_disk = read_file (file, &length);
  g_assert_cmpmem (on_disk, length, latin1, strlen (latin1));
}

static void
test_save_untitled_needs_target (void)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autoptr (GError) error = NULL;

  g_assert_false (save (document, NULL, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

/* "Save As" on a new document: writes the file and adopts its name. */
/* "saved" comes after the location was updated. */
static void
on_saved (PalangDocument *document,
          GFile         **file_when_saved)
{
  g_set_object (file_when_saved, palang_document_get_file (document));
}

static void
test_save_as (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autofree char *path = g_build_filename (fixture->dir, "new.c", NULL);
  g_autoptr (GFile) target = g_file_new_for_path (path);
  g_autoptr (GError) error = NULL;
  g_autofree char *on_disk = NULL;
  g_autofree char *title = NULL;
  g_autoptr (GFile) file_when_saved = NULL;
  GtkSourceLanguage *language;

  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (document), "int x;\n", -1);
  g_signal_connect (document, "saved", G_CALLBACK (on_saved), &file_when_saved);

  g_assert_true (save (document, target, &error));
  g_assert_no_error (error);
  g_assert_nonnull (file_when_saved);
  g_assert_true (g_file_equal (file_when_saved, target));

  on_disk = read_file (target, NULL);
  title = palang_document_dup_title (document);
  language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (document));

  g_assert_cmpstr (on_disk, ==, "int x;\n");
  g_assert_cmpstr (title, ==, "new.c");
  g_assert_true (g_file_equal (palang_document_get_file (document), target));
  g_assert_false (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (document)));
  g_assert_nonnull (language);
  g_assert_cmpstr (gtk_source_language_get_id (language), ==, "c");
}

/* Saving must refuse to overwrite a file another program changed after we
 * loaded it, unless forced. */
static void
test_save_detects_external_change (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autoptr (GFile) file = write_file (fixture, "shared.txt", "ours\n", -1);
  g_autoptr (GError) error = NULL;
  g_autoptr (GAsyncResult) result = NULL;
  g_autofree char *path = g_file_get_path (file);
  g_autofree char *on_disk = NULL;

  g_assert_true (load (document, file, &error));

  /* Another program writes the file; push its modification time forward so
   * the change is visible even within the same second. */
  g_file_set_contents (path, "theirs\n", -1, NULL);
  g_file_set_attribute_uint64 (file, G_FILE_ATTRIBUTE_TIME_MODIFIED,
                               g_get_real_time () / G_USEC_PER_SEC + 10,
                               G_FILE_QUERY_INFO_NONE, NULL, &error);
  g_assert_no_error (error);

  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (document), "ours, edited\n", -1);

  g_assert_false (save (document, NULL, &error));
  g_assert_error (error, GTK_SOURCE_FILE_SAVER_ERROR,
                  GTK_SOURCE_FILE_SAVER_ERROR_EXTERNALLY_MODIFIED);

  palang_document_save_async (document, NULL, TRUE, NULL, store_result, &result);
  wait_for_result (&result);
  g_clear_error (&error);
  g_assert_true (palang_document_save_finish (document, result, &error));
  g_assert_no_error (error);

  on_disk = read_file (file, NULL);
  g_assert_cmpstr (on_disk, ==, "ours, edited\n");
}

/* ---- Watching for outside changes --------------------------------------- */

typedef struct {
  guint    count;
  gboolean deleted;
} DiskChanges;

static void
on_changed_on_disk (PalangDocument *document,
                    gboolean        deleted,
                    DiskChanges    *changes)
{
  changes->count++;
  changes->deleted = deleted;
}

/* Runs the main loop for @ms milliseconds. */
static void
run_main_loop_for (guint ms)
{
  gint64 until = g_get_monotonic_time () + ms * 1000;

  while (g_get_monotonic_time () < until)
    g_main_context_iteration (NULL, FALSE);
}

/* Another program writes the file. The modification time is pushed
 * forward so the change is visible even within the same second. */
static void
write_externally (GFile *file, const char *contents)
{
  g_autofree char *path = g_file_get_path (file);
  g_autoptr (GError) error = NULL;

  g_file_set_contents (path, contents, -1, &error);
  g_assert_no_error (error);
  g_file_set_attribute_uint64 (file, G_FILE_ATTRIBUTE_TIME_MODIFIED,
                               g_get_real_time () / G_USEC_PER_SEC + 10,
                               G_FILE_QUERY_INFO_NONE, NULL, &error);
  g_assert_no_error (error);
}

static void
test_detects_outside_change (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autoptr (GFile) file = write_file (fixture, "watched.txt", "v1\n", -1);
  DiskChanges changes = { 0 };

  g_assert_true (load (document, file, NULL));
  g_signal_connect (document, "changed-on-disk", G_CALLBACK (on_changed_on_disk), &changes);

  write_externally (file, "v2\n");
  run_main_loop_for (1500);

  g_assert_cmpuint (changes.count, >=, 1);
  g_assert_false (changes.deleted);
}

/* Our own saves must not look like outside changes. */
static void
test_own_save_is_not_an_outside_change (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autoptr (GFile) file = write_file (fixture, "mine.txt", "v1\n", -1);
  DiskChanges changes = { 0 };

  g_assert_true (load (document, file, NULL));
  g_signal_connect (document, "changed-on-disk", G_CALLBACK (on_changed_on_disk), &changes);

  for (int i = 0; i < 3; i++)
    {
      gtk_text_buffer_insert_at_cursor (GTK_TEXT_BUFFER (document), "x", -1);
      g_assert_true (save (document, NULL, NULL));
    }
  run_main_loop_for (1500);

  g_assert_cmpuint (changes.count, ==, 0);
}

static void
test_detects_deletion (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangDocument) document = palang_document_new ();
  g_autoptr (GFile) file = write_file (fixture, "doomed.txt", "bye\n", -1);
  g_autofree char *path = g_file_get_path (file);
  DiskChanges changes = { 0 };

  g_assert_true (load (document, file, NULL));
  g_signal_connect (document, "changed-on-disk", G_CALLBACK (on_changed_on_disk), &changes);

  g_unlink (path);
  run_main_loop_for (1500);

  g_assert_cmpuint (changes.count, >=, 1);
  g_assert_true (changes.deleted);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  gtk_source_init ();

  g_test_add_func ("/document/new", test_new_document);
  g_test_add_func ("/document/save-untitled-needs-target", test_save_untitled_needs_target);
  g_test_add ("/document/load", Fixture, NULL,
              fixture_setup, test_load, fixture_teardown);
  g_test_add ("/document/load-missing-file", Fixture, NULL,
              fixture_setup, test_load_missing_file, fixture_teardown);
  g_test_add ("/document/save-keeps-crlf", Fixture, NULL,
              fixture_setup, test_save_keeps_crlf, fixture_teardown);
  g_test_add ("/document/save-keeps-encoding", Fixture, "caf\xe9 au lait\n",
              fixture_setup, test_save_keeps_encoding, fixture_teardown);
  /* Non-UTF-8 byte at the very end of the file (see on_load_finished). */
  g_test_add ("/document/save-keeps-encoding-at-end", Fixture, "x = \"caf\xe9\"\n",
              fixture_setup, test_save_keeps_encoding, fixture_teardown);
  g_test_add ("/document/save-as", Fixture, NULL,
              fixture_setup, test_save_as, fixture_teardown);
  g_test_add ("/document/save-detects-external-change", Fixture, NULL,
              fixture_setup, test_save_detects_external_change, fixture_teardown);
  g_test_add ("/document/detects-outside-change", Fixture, NULL,
              fixture_setup, test_detects_outside_change, fixture_teardown);
  g_test_add ("/document/own-save-is-not-an-outside-change", Fixture, NULL,
              fixture_setup, test_own_save_is_not_an_outside_change, fixture_teardown);
  g_test_add ("/document/detects-deletion", Fixture, NULL,
              fixture_setup, test_detects_deletion, fixture_teardown);

  return g_test_run ();
}
