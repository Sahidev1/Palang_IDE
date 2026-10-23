/* test-complaints.c — the complaints file and the complaint box.
 *
 * The file functions need no display; the dialog tests do (it's in the
 * `ui_tests` list). Typing with real keys and Ctrl+Enter: MANUAL_TESTS.md.
 */

#include "config.h"

#include <string.h>

#include <glib/gstdio.h>

#include "palang-complaint-dialog.h"
#include "palang-complaints.h"

typedef struct {
  char  *dir;
  GFile *file;   /* dir/notes/complaints.md: the folder doesn't exist yet */
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  g_autofree char *path = NULL;

  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", NULL);
  path = g_build_filename (fixture->dir, "notes", "complaints.md", NULL);
  fixture->file = g_file_new_for_path (path);
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_autofree char *notes = g_build_filename (fixture->dir, "notes", NULL);

  g_file_delete (fixture->file, NULL, NULL);
  g_rmdir (notes);
  g_rmdir (fixture->dir);
  g_clear_object (&fixture->file);
  g_free (fixture->dir);
}

/* Returns: (transfer full): the file's contents, or NULL if there is none. */
static char *
read_file (Fixture *fixture)
{
  char *contents = NULL;

  if (!g_file_load_contents (fixture->file, NULL, &contents, NULL, NULL, NULL))
    return NULL;
  return contents;
}

/* ---- The file ----------------------------------------------------------- */

static void
test_get_file (void)
{
  g_autoptr (GFile) fallback = palang_complaints_get_file ("");
  g_autoptr (GFile) home = palang_complaints_get_file ("~/notes/c.md");
  g_autoptr (GFile) relative = palang_complaints_get_file ("notes/c.md");
  g_autoptr (GFile) absolute = palang_complaints_get_file ("/tmp/c.md");
  g_autofree char *fallback_path = g_build_filename (g_get_user_data_dir (), "palang",
                                                     "complaints.md", NULL);
  g_autofree char *home_path = g_build_filename (g_get_home_dir (), "notes", "c.md", NULL);
  g_autofree char *path = NULL;

  path = g_file_get_path (fallback);
  g_assert_cmpstr (path, ==, fallback_path);
  g_free (path);
  path = g_file_get_path (home);
  g_assert_cmpstr (path, ==, home_path);
  g_free (path);
  path = g_file_get_path (relative);
  g_assert_cmpstr (path, ==, home_path);
  g_free (path);
  path = g_file_get_path (absolute);
  g_assert_cmpstr (path, ==, "/tmp/c.md");
}

static void
test_format_entry (void)
{
  g_autoptr (GTimeZone) zone = g_time_zone_new_local ();
  g_autoptr (GDateTime) time = g_date_time_new (zone, 2026, 10, 23, 14, 5, 0);
  g_autofree char *with_context = palang_complaints_format_entry (time, "src/main.c:42",
                                                                  "  Too slow.\n\n");
  g_autofree char *without = palang_complaints_format_entry (time, NULL, "Two\nlines");

  g_assert_cmpstr (with_context, ==, "\n## 2026-10-23 14:05 · src/main.c:42\n\nToo slow.\n");
  g_assert_cmpstr (without, ==, "\n## 2026-10-23 14:05\n\nTwo\nlines\n");
}

/* The first entry creates the folder and the file with its heading; the
 * next one goes below, without a second heading. */
static void
test_append (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *contents = NULL;

  g_assert_true (palang_complaints_append (fixture->file, "\n## one\n\nFirst.\n", &error));
  g_assert_no_error (error);
  g_assert_true (palang_complaints_append (fixture->file, "\n## two\n\nSecond.\n", &error));
  g_assert_no_error (error);

  contents = read_file (fixture);
  g_assert_true (g_str_has_prefix (contents, "# Complaints about Palang\n"));
  g_assert_true (g_str_has_suffix (contents, "\n## one\n\nFirst.\n\n## two\n\nSecond.\n"));
  g_assert_null (strstr (strstr (contents, "## one"), "# Complaints"));
}

/* ---- The dialog --------------------------------------------------------- */

static void
on_sent (PalangComplaintDialog *dialog,
         gboolean              *sent)
{
  *sent = TRUE;
}

/* Returns: (transfer none): the dialog's text box (its focus widget). */
static GtkTextBuffer *
get_buffer (PalangComplaintDialog *dialog)
{
  GtkWidget *focus = NULL;
  GtkTextBuffer *buffer;

  g_object_get (dialog, "focus-widget", &focus, NULL);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (focus));
  g_object_unref (focus);  /* the dialog keeps it alive */
  return buffer;
}

/* Send is off while the text is blank; sending writes the entry with the
 * context, emits "sent" and closes the dialog. */
static void
test_dialog_sends (Fixture *fixture, gconstpointer data)
{
  GtkWidget *window = adw_window_new ();
  PalangComplaintDialog *dialog = palang_complaint_dialog_new (fixture->file, "src/main.c:42");
  GtkTextBuffer *buffer = get_buffer (dialog);
  gboolean sent = FALSE;
  g_autofree char *contents = NULL;

  g_object_ref_sink (dialog);
  gtk_window_present (GTK_WINDOW (window));
  g_signal_connect (dialog, "sent", G_CALLBACK (on_sent), &sent);
  adw_dialog_present (ADW_DIALOG (dialog), window);

  /* An action that is off doesn't run: nothing is written. */
  gtk_text_buffer_set_text (buffer, "  \n ", -1);
  gtk_widget_activate_action (GTK_WIDGET (dialog), "complaint.send", NULL);
  g_assert_false (sent);
  g_assert_false (g_file_query_exists (fixture->file, NULL));

  gtk_text_buffer_set_text (buffer, "The tab closed.", -1);
  gtk_widget_activate_action (GTK_WIDGET (dialog), "complaint.send", NULL);
  g_assert_true (sent);

  contents = read_file (fixture);
  g_assert_nonnull (strstr (contents, " · src/main.c:42\n\nThe tab closed.\n"));

  g_object_unref (dialog);
  gtk_window_destroy (GTK_WINDOW (window));
}

/* If the file can't be written, the dialog stays open with the text. */
static void
test_dialog_keeps_text_on_error (Fixture *fixture, gconstpointer data)
{
  /* notes/ is made a file below, so complaints.md can't be created in it. */
  g_autofree char *notes = g_build_filename (fixture->dir, "notes", NULL);
  GtkWidget *window = adw_window_new ();
  PalangComplaintDialog *dialog = palang_complaint_dialog_new (fixture->file, NULL);
  GtkTextBuffer *buffer = get_buffer (dialog);
  gboolean sent = FALSE;
  GtkTextIter start, end;
  g_autofree char *text = NULL;

  g_object_ref_sink (dialog);
  gtk_window_present (GTK_WINDOW (window));
  g_signal_connect (dialog, "sent", G_CALLBACK (on_sent), &sent);
  adw_dialog_present (ADW_DIALOG (dialog), window);

  g_assert_true (g_file_set_contents (notes, "", 0, NULL));
  gtk_text_buffer_set_text (buffer, "Lost?", -1);
  gtk_widget_activate_action (GTK_WIDGET (dialog), "complaint.send", NULL);

  g_assert_false (sent);
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  g_assert_cmpstr (text, ==, "Lost?");

  adw_dialog_force_close (ADW_DIALOG (dialog));
  g_object_unref (dialog);
  g_unlink (notes);
  gtk_window_destroy (GTK_WINDOW (window));
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  adw_init ();
  g_object_set (gtk_settings_get_default (), "gtk-enable-animations", FALSE, NULL);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  g_test_add_func ("/complaints/get-file", test_get_file);
  g_test_add_func ("/complaints/format-entry", test_format_entry);
  ADD_TEST ("/complaints/append", test_append);
  ADD_TEST ("/complaints/dialog-sends", test_dialog_sends);
  ADD_TEST ("/complaints/dialog-keeps-text-on-error", test_dialog_keeps_text_on_error);

#undef ADD_TEST

  return g_test_run ();
}
