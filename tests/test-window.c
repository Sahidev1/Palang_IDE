/* test-window.c — UI tests for PalangWindow: tabs and file actions.
 *
 * These need a display (GTK can't start without one), but the window is
 * never shown, so nothing appears on screen. Under `meson test` they run on
 * a virtual display (xvfb-run) when it's installed.
 *
 * The unsaved-changes prompts are tested by clicking their buttons
 * (answer_dialog). The Save As file chooser is a separate system dialog and
 * is covered by MANUAL_TESTS.md instead.
 */

#include "config.h"

#include <glib/gstdio.h>

#include "palang-palette.h"
#include "palang-settings.h"
#include "palang-window.h"

/* ---- Helpers ------------------------------------------------------------ */

typedef gboolean (*Condition) (PalangWindow *window, gpointer data);

/* Runs the main loop until @condition is TRUE, failing after 5 seconds. */
static void
wait_until (PalangWindow *window,
            Condition     condition,
            gpointer      data)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  while (!condition (window, data))
    {
      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out waiting for a condition");
      g_main_context_iteration (NULL, FALSE);
    }
}

/* Runs the main loop for a fixed time, letting pending work happen. */
static void
run_main_loop_for (gint64 microseconds)
{
  gint64 until = g_get_monotonic_time () + microseconds;

  while (g_get_monotonic_time () < until)
    g_main_context_iteration (NULL, FALSE);
}

/* Lets pending async work run for a moment, for checking that something
 * does *not* happen. */
static void
run_main_loop_briefly (void)
{
  run_main_loop_for (G_USEC_PER_SEC / 5);
}

static gboolean
active_file_is (PalangWindow *window, gpointer file)
{
  PalangEditor *editor = palang_window_get_active_editor (window);
  GFile *active_file;

  if (editor == NULL)
    return FALSE;

  active_file = palang_document_get_file (palang_editor_get_document (editor));
  return active_file != NULL && g_file_equal (active_file, file);
}

static gboolean
active_document_unmodified (PalangWindow *window, gpointer data)
{
  PalangEditor *editor = palang_window_get_active_editor (window);

  return !gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (palang_editor_get_document (editor)));
}

static void run_main_loop_for (gint64 microseconds);

static gboolean
dialog_is_visible (PalangWindow *window, gpointer data)
{
  return adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window)) != NULL;
}

static gboolean window_is_open (PalangWindow *window);

/* The dialog closed — or the whole window did (e.g. after "Save All"). */
static gboolean
dialog_is_gone (PalangWindow *window, gpointer data)
{
  return !window_is_open (window) || !dialog_is_visible (window, data);
}

/* Returns: (transfer none) (nullable): the first GtkButton inside @widget
 * whose label is @label, searching child widgets recursively. */
static GtkWidget *
find_button (GtkWidget  *widget,
             const char *label)
{
  if (GTK_IS_BUTTON (widget) &&
      g_strcmp0 (gtk_button_get_label (GTK_BUTTON (widget)), label) == 0)
    return widget;

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *found = find_button (child, label);

      if (found != NULL)
        return found;
    }

  return NULL;
}

/* Waits for the window's alert dialog and clicks the button labelled
 * @label (e.g. "_Cancel", "_Discard", "_Save"), like a user would. Tests
 * using this must call show_window() first: dialogs need a shown window. */
static void
answer_dialog (PalangWindow *window,
               const char   *label)
{
  AdwDialog *dialog;
  GtkWidget *button;

  wait_until (window, dialog_is_visible, NULL);
  dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window));
  g_assert_true (ADW_IS_ALERT_DIALOG (dialog));

  /* Like a person, click only after the dialog has finished appearing.
   * Clicking in the same instant it appears makes libadwaita deliver the
   * response but never finish closing the dialog (seen under Xvfb; real
   * clicks in the running app close it fine). There's no signal for
   * "finished appearing", so we give it half a second. */
  run_main_loop_for (G_USEC_PER_SEC / 2);

  button = find_button (GTK_WIDGET (dialog), label);
  g_assert_nonnull (button);
  g_signal_emit_by_name (button, "clicked");

  wait_until (window, dialog_is_gone, NULL);
}

static gboolean
window_is_mapped (PalangWindow *window, gpointer data)
{
  return gtk_widget_get_mapped (GTK_WIDGET (window));
}

/* Shows the window and waits until it's on screen, which dialogs need.
 * Under xvfb-run it appears on the virtual display; otherwise it briefly
 * flashes on screen. */
static void
show_window (PalangWindow *window)
{
  gtk_window_present (GTK_WINDOW (window));
  wait_until (window, window_is_mapped, NULL);
}

/* Inserts text into the active document, making it modified. */
static void
type_into_active (PalangWindow *window,
                  const char   *text)
{
  PalangEditor *editor = palang_window_get_active_editor (window);
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (palang_editor_get_document (editor));
  GtkTextIter end;

  gtk_text_buffer_get_end_iter (buffer, &end);
  gtk_text_buffer_insert (buffer, &end, text, -1);
}

/* TRUE while @window is one of GTK's open toplevel windows. (We can't
 * watch the "destroy" signal: it only fires once the last reference is
 * dropped, and the test fixture holds one.) */
static gboolean
window_is_open (PalangWindow *window)
{
  GListModel *toplevels = gtk_window_get_toplevels ();
  guint n = g_list_model_get_n_items (toplevels);

  for (guint i = 0; i < n; i++)
    {
      g_autoptr (GObject) toplevel = g_list_model_get_item (toplevels, i);

      if (toplevel == G_OBJECT (window))
        return TRUE;
    }

  return FALSE;
}

static gboolean
window_is_closed (PalangWindow *window, gpointer data)
{
  return !window_is_open (window);
}

static gboolean
n_editors_is (PalangWindow *window, gpointer n)
{
  return palang_window_get_n_editors (window) == GPOINTER_TO_UINT (n);
}

typedef struct {
  char         *dir;
  PalangWindow *window;
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GError) error = NULL;

  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", &error);
  g_assert_no_error (error);

  /* g_object_ref_sink: windows start with a "floating" reference; sink it
   * so the fixture owns the window like a normal reference. */
  fixture->window = g_object_ref_sink (palang_window_new (NULL));
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GDir) dir = g_dir_open (fixture->dir, 0, NULL);
  const char *name;

  gtk_window_destroy (GTK_WINDOW (fixture->window));
  g_clear_object (&fixture->window);

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *path = g_build_filename (fixture->dir, name, NULL);
      g_unlink (path);
    }
  g_rmdir (fixture->dir);
  g_free (fixture->dir);
}

static GFile *
write_file (Fixture *fixture, const char *name, const char *contents)
{
  g_autofree char *path = g_build_filename (fixture->dir, name, NULL);
  g_autoptr (GError) error = NULL;

  g_file_set_contents (path, contents, -1, &error);
  g_assert_no_error (error);
  return g_file_new_for_path (path);
}

/* ---- Tests -------------------------------------------------------------- */

static void
test_starts_empty (Fixture *fixture, gconstpointer data)
{
  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 0);
  g_assert_null (palang_window_get_active_editor (fixture->window));
}

static void
test_new_document (Fixture *fixture, gconstpointer data)
{
  g_autofree char *title = NULL;

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.new", NULL);

  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 1);
  title = palang_editor_dup_title (palang_window_get_active_editor (fixture->window));
  g_assert_cmpstr (title, ==, "Untitled");
  g_assert_cmpstr (gtk_window_get_title (GTK_WINDOW (fixture->window)), ==, "Untitled — Palang");
}

/* Opening a file replaces a blank "Untitled" tab instead of adding one. */
static void
test_open_replaces_blank_untitled (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) file = write_file (fixture, "a.c", "int a;\n");

  palang_window_new_document (fixture->window);
  palang_window_open_file (fixture->window, file);
  wait_until (fixture->window, active_file_is, file);

  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 1);
}

/* Opening a file that's already open switches to its tab. */
static void
test_open_twice_reuses_tab (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) a = write_file (fixture, "a.c", "int a;\n");
  g_autoptr (GFile) b = write_file (fixture, "b.c", "int b;\n");

  palang_window_open_file (fixture->window, a);
  wait_until (fixture->window, active_file_is, a);
  palang_window_open_file (fixture->window, b);
  wait_until (fixture->window, active_file_is, b);
  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 2);

  palang_window_open_file (fixture->window, a);
  g_assert_true (active_file_is (fixture->window, a));
  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 2);
}

/* Editing marks the tab with "•"; win.save writes the file and clears it. */
static void
test_edit_and_save (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) file = write_file (fixture, "notes.txt", "hello\n");
  g_autofree char *modified_title = NULL;
  g_autofree char *saved_title = NULL;
  g_autofree char *on_disk = NULL;
  PalangEditor *editor;
  GtkTextBuffer *buffer;
  GtkTextIter end;

  palang_window_open_file (fixture->window, file);
  wait_until (fixture->window, active_file_is, file);

  editor = palang_window_get_active_editor (fixture->window);
  buffer = GTK_TEXT_BUFFER (palang_editor_get_document (editor));
  gtk_text_buffer_get_end_iter (buffer, &end);
  gtk_text_buffer_insert (buffer, &end, "world\n", -1);

  modified_title = palang_editor_dup_title (editor);
  g_assert_cmpstr (modified_title, ==, "• notes.txt");
  g_assert_cmpstr (gtk_window_get_title (GTK_WINDOW (fixture->window)), ==, "• notes.txt — Palang");

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.save", NULL);
  wait_until (fixture->window, active_document_unmodified, NULL);

  saved_title = palang_editor_dup_title (editor);
  g_assert_cmpstr (saved_title, ==, "notes.txt");

  g_file_load_contents (file, NULL, &on_disk, NULL, NULL, NULL);
  g_assert_cmpstr (on_disk, ==, "hello\nworld\n");
}

/* An unmodified tab closes right away, without a prompt. */
static void
test_close_unmodified_tab (Fixture *fixture, gconstpointer data)
{
  palang_window_new_document (fixture->window);
  palang_window_new_document (fixture->window);
  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 2);

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.close-tab", NULL);
  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 1);
}

/* A file that can't be opened adds no tab (the error is shown as a toast). */
static void
test_open_missing_file (Fixture *fixture, gconstpointer data)
{
  g_autofree char *path = g_build_filename (fixture->dir, "missing.c", NULL);
  g_autoptr (GFile) missing = g_file_new_for_path (path);

  palang_window_open_file (fixture->window, missing);
  run_main_loop_briefly ();

  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 0);
}

/* Closing a modified tab asks first. Cancel keeps it, Discard closes it. */
static void
test_close_modified_tab_cancel_then_discard (Fixture *fixture, gconstpointer data)
{
  show_window (fixture->window);
  palang_window_new_document (fixture->window);
  type_into_active (fixture->window, "draft");

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.close-tab", NULL);
  answer_dialog (fixture->window, "_Cancel");
  run_main_loop_briefly ();
  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 1);

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.close-tab", NULL);
  answer_dialog (fixture->window, "_Discard");
  wait_until (fixture->window, n_editors_is, GUINT_TO_POINTER (0));
}

/* "Save" in the close prompt writes the file, then closes the tab. */
static void
test_close_modified_tab_save (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) file = write_file (fixture, "keep.txt", "v1\n");
  g_autofree char *on_disk = NULL;

  show_window (fixture->window);
  palang_window_open_file (fixture->window, file);
  wait_until (fixture->window, active_file_is, file);
  type_into_active (fixture->window, "v2\n");

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.close-tab", NULL);
  answer_dialog (fixture->window, "_Save");
  wait_until (fixture->window, n_editors_is, GUINT_TO_POINTER (0));

  g_file_load_contents (file, NULL, &on_disk, NULL, NULL, NULL);
  g_assert_cmpstr (on_disk, ==, "v1\nv2\n");
}

/* Closing the window with unsaved changes asks first; Cancel keeps it open
 * with the changes, "Save All" saves and then closes. */
static void
test_close_window_with_changes (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) a = write_file (fixture, "a.txt", "a\n");
  g_autoptr (GFile) b = write_file (fixture, "b.txt", "b\n");
  g_autofree char *a_on_disk = NULL;
  g_autofree char *b_on_disk = NULL;

  show_window (fixture->window);
  palang_window_open_file (fixture->window, a);
  wait_until (fixture->window, active_file_is, a);
  type_into_active (fixture->window, "a2\n");
  palang_window_open_file (fixture->window, b);
  wait_until (fixture->window, active_file_is, b);
  type_into_active (fixture->window, "b2\n");


  gtk_window_close (GTK_WINDOW (fixture->window));
  answer_dialog (fixture->window, "_Cancel");
  run_main_loop_briefly ();
  g_assert_true (window_is_open (fixture->window));
  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 2);

  gtk_window_close (GTK_WINDOW (fixture->window));
  answer_dialog (fixture->window, "_Save All");
  wait_until (fixture->window, window_is_closed, NULL);

  g_file_load_contents (a, NULL, &a_on_disk, NULL, NULL, NULL);
  g_file_load_contents (b, NULL, &b_on_disk, NULL, NULL, NULL);
  g_assert_cmpstr (a_on_disk, ==, "a\na2\n");
  g_assert_cmpstr (b_on_disk, ==, "b\nb2\n");
}

/* ---- Changes by other programs ----------------------------------------- */

/* Another program writes the file (with a later modification time, so the
 * change is visible even within the same second). */
static void
write_externally (GFile *file, const char *contents)
{
  g_autofree char *path = g_file_get_path (file);

  g_file_set_contents (path, contents, -1, NULL);
  g_file_set_attribute_uint64 (file, G_FILE_ATTRIBUTE_TIME_MODIFIED,
                               g_get_real_time () / G_USEC_PER_SEC + 10,
                               G_FILE_QUERY_INFO_NONE, NULL, NULL);
}

static gboolean
active_text_is (PalangWindow *window, gpointer expected)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (palang_editor_get_document (palang_window_get_active_editor (window)));
  GtkTextIter start, end;
  g_autofree char *text = NULL;

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
  return g_strcmp0 (text, expected) == 0;
}

static gboolean
disk_banner_visible (PalangWindow *window, gpointer data)
{
  return palang_editor_is_disk_banner_visible (palang_window_get_active_editor (window));
}

/* No unsaved changes: the new version simply appears. */
static void
test_reloads_unmodified_file (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) file = write_file (fixture, "live.txt", "old\n");

  palang_window_open_file (fixture->window, file);
  wait_until (fixture->window, active_file_is, file);

  write_externally (file, "new\n");
  wait_until (fixture->window, active_text_is, "new\n");
  g_assert_false (disk_banner_visible (fixture->window, NULL));
}

/* Unsaved changes are never thrown away silently: a banner asks first. */
static void
test_modified_file_shows_banner (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) file = write_file (fixture, "both.txt", "old\n");

  palang_window_open_file (fixture->window, file);
  wait_until (fixture->window, active_file_is, file);
  type_into_active (fixture->window, "mine\n");

  write_externally (file, "theirs\n");
  wait_until (fixture->window, disk_banner_visible, NULL);
  g_assert_true (active_text_is (fixture->window, "old\nmine\n"));

  /* The banner's Reload button. */
  palang_editor_reload_from_disk (palang_window_get_active_editor (fixture->window));
  wait_until (fixture->window, active_text_is, "theirs\n");
  g_assert_false (disk_banner_visible (fixture->window, NULL));
}

static void
test_deleted_file_shows_banner (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) file = write_file (fixture, "gone.txt", "text\n");
  g_autofree char *path = g_file_get_path (file);
  g_autofree char *title = NULL;

  palang_window_open_file (fixture->window, file);
  wait_until (fixture->window, active_file_is, file);

  g_unlink (path);
  wait_until (fixture->window, disk_banner_visible, NULL);

  /* Marked modified, so closing would ask first. */
  title = palang_editor_dup_title (palang_window_get_active_editor (fixture->window));
  g_assert_cmpstr (title, ==, "• gone.txt");
}

/* ---- Session ------------------------------------------------------------ */

static void
test_dup_session (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) folder = g_file_new_for_path (fixture->dir);
  g_autoptr (GFile) a = write_file (fixture, "a.c", "");
  g_autoptr (GFile) b = write_file (fixture, "b.c", "");
  g_autoptr (PalangSession) session = NULL;

  palang_window_open_folder (fixture->window, folder);
  palang_window_open_file (fixture->window, a);
  wait_until (fixture->window, active_file_is, a);
  palang_window_new_document (fixture->window);  /* Untitled: not saved */
  palang_window_open_file (fixture->window, b);
  wait_until (fixture->window, active_file_is, b);

  session = palang_window_dup_session (fixture->window);

  g_assert_true (g_file_equal (session->folder, folder));
  g_assert_cmpuint (session->files->len, ==, 2);
  g_assert_true (g_file_equal (g_ptr_array_index (session->files, 0), a));
  g_assert_true (g_file_equal (g_ptr_array_index (session->files, 1), b));
  g_assert_cmpint (session->active, ==, 1);
  g_assert_true (session->show_sidebar);
}

static gboolean
has_n_editors_and_active (PalangWindow *window, gpointer file)
{
  return palang_window_get_n_editors (window) == 2 && active_file_is (window, file);
}

/* Tabs come back in order, the active one selected; missing files are
 * skipped. */
static void
test_restore_session (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSession) session = palang_session_new ();
  g_autoptr (GFile) a = write_file (fixture, "a.c", "");
  g_autoptr (GFile) b = write_file (fixture, "b.c", "");
  g_autofree char *missing_path = g_build_filename (fixture->dir, "missing.c", NULL);
  PalangEditor *first;

  session->folder = g_file_new_for_path (fixture->dir);
  g_ptr_array_add (session->files, g_object_ref (a));
  g_ptr_array_add (session->files, g_file_new_for_path (missing_path));
  g_ptr_array_add (session->files, g_object_ref (b));
  session->active = 0;
  session->show_sidebar = TRUE;

  palang_window_restore_session (fixture->window, session);
  wait_until (fixture->window, has_n_editors_and_active, a);
  run_main_loop_briefly ();  /* nothing else should change afterwards */

  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 2);
  g_assert_true (active_file_is (fixture->window, a));
  g_assert_true (g_file_equal (palang_window_get_folder (fixture->window), session->folder));

  /* Order: a.c, then b.c. */
  first = palang_window_get_active_editor (fixture->window);
  g_assert_true (g_file_equal (palang_document_get_file (palang_editor_get_document (first)), a));
  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.close-tab", NULL);
  g_assert_true (active_file_is (fixture->window, b));
}

/* ---- Bottom panel and active editor ------------------------------------- */

static void
test_toggle_panel (Fixture *fixture, gconstpointer data)
{
  GtkWidget *panel = GTK_WIDGET (palang_window_get_panel (fixture->window));

  g_assert_false (gtk_widget_get_visible (panel));
  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.toggle-panel", NULL);
  g_assert_true (gtk_widget_get_visible (panel));
  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.toggle-panel", NULL);
  g_assert_false (gtk_widget_get_visible (panel));
}

/* Within 2 pixels: the divider's handle takes a pixel of the space. */
static gboolean
panel_height_is (PalangWindow *window, gpointer height)
{
  int actual = gtk_widget_get_height (GTK_WIDGET (palang_window_get_panel (window)));

  return ABS (actual - GPOINTER_TO_INT (height)) <= 2;
}

/* The panel's state goes into the session and comes back from it,
 * including its height on screen. */
static void
test_panel_session (Fixture *fixture, gconstpointer data)
{
  PalangPanel *panel = palang_window_get_panel (fixture->window);
  g_autoptr (PalangSession) session = NULL;
  g_autoptr (PalangWindow) restored = NULL;
  PalangPanel *restored_panel;

  palang_panel_add_page (panel, gtk_label_new ("A"), "a", "A", NULL);
  palang_panel_add_page (panel, gtk_label_new ("B"), "b", "B", NULL);
  palang_panel_set_visible_page (panel, "b");
  palang_panel_set_visible_page (panel, "no-such-page");  /* ignored */
  gtk_widget_set_visible (GTK_WIDGET (panel), TRUE);

  /* Not shown on screen: the default height. */
  session = palang_window_dup_session (fixture->window);
  g_assert_true (session->show_panel);
  g_assert_cmpint (session->panel_height, ==, 250);
  g_assert_cmpstr (session->panel_page, ==, "b");
  session->panel_height = 180;

  /* A new window with the same pages gets the panel back as it was. */
  restored = g_object_ref_sink (palang_window_new (NULL));
  restored_panel = palang_window_get_panel (restored);
  palang_panel_add_page (restored_panel, gtk_label_new ("A"), "a", "A", NULL);
  palang_panel_add_page (restored_panel, gtk_label_new ("B"), "b", "B", NULL);
  palang_window_restore_session (restored, session);

  g_assert_true (gtk_widget_get_visible (GTK_WIDGET (restored_panel)));
  g_assert_cmpstr (palang_panel_get_visible_page (restored_panel), ==, "b");

  /* On screen, the panel gets the saved height, and saving again gives the
   * same number (no pixel lost to the divider each time). */
  show_window (restored);
  wait_until (restored, panel_height_is, GINT_TO_POINTER (180));
  g_clear_pointer (&session, palang_session_free);
  session = palang_window_dup_session (restored);
  g_assert_cmpint (session->panel_height, ==, 180);

  gtk_window_destroy (GTK_WINDOW (restored));
}

static void
count_notify (GObject *object, GParamSpec *pspec, gpointer count)
{
  (*(guint *) count)++;
}

/* "notify::active-editor" fires when the selected tab changes. */
static void
test_active_editor_property (Fixture *fixture, gconstpointer data)
{
  guint notifications = 0;
  PalangEditor *first;
  g_autoptr (PalangEditor) from_property = NULL;

  g_signal_connect (fixture->window, "notify::active-editor",
                    G_CALLBACK (count_notify), &notifications);

  palang_window_new_document (fixture->window);
  first = palang_window_get_active_editor (fixture->window);
  g_assert_cmpuint (notifications, ==, 1);

  palang_window_new_document (fixture->window);
  g_assert_cmpuint (notifications, ==, 2);
  g_assert_true (palang_window_get_active_editor (fixture->window) != first);

  g_object_get (fixture->window, "active-editor", &from_property, NULL);
  g_assert_true (from_property == palang_window_get_active_editor (fixture->window));

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.close-tab", NULL);
  g_assert_true (palang_window_get_active_editor (fixture->window) == first);
  g_assert_cmpuint (notifications, ==, 3);
}

/* The terminal's shell starts in the project folder. */
static void
test_terminal_follows_folder (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) folder = g_file_new_for_path (fixture->dir);
  PalangTerminal *terminal = palang_window_get_terminal (fixture->window);

  g_assert_null (palang_terminal_get_directory (terminal));
  palang_window_open_folder (fixture->window, folder);
  g_assert_true (g_file_equal (palang_terminal_get_directory (terminal), folder));
}

static gboolean
cursor_line_is_visible (PalangWindow *window, gpointer line)
{
  PalangEditor *editor = palang_window_get_active_editor (window);
  GtkTextView *view;
  GtkTextBuffer *buffer;
  GtkTextIter cursor;
  GdkRectangle visible, location;

  if (editor == NULL)
    return FALSE;

  view = GTK_TEXT_VIEW (palang_editor_get_view (editor));
  buffer = gtk_text_view_get_buffer (view);
  gtk_text_buffer_get_iter_at_mark (buffer, &cursor, gtk_text_buffer_get_insert (buffer));
  if (gtk_text_iter_get_line (&cursor) + 1 != GPOINTER_TO_INT (line))
    return FALSE;

  gtk_text_view_get_visible_rect (view, &visible);
  gtk_text_view_get_iter_location (view, &cursor, &location);
  return visible.height > 0 &&
         location.y >= visible.y && location.y + location.height <= visible.y + visible.height;
}

/* Opening a file at a line (what a search result does) puts the cursor
 * there and scrolls it into view, also in a tab that was just created. */
static void
test_open_file_at (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GString) contents = g_string_new (NULL);
  g_autoptr (GFile) file = NULL;
  GtkTextIter cursor;
  GtkTextBuffer *buffer;

  for (guint i = 1; i <= 300; i++)
    g_string_append_printf (contents, "line %u\n", i);
  file = write_file (fixture, "long.txt", contents->str);

  show_window (fixture->window);
  palang_window_open_file_at (fixture->window, file, 200, 3);
  wait_until (fixture->window, cursor_line_is_visible, GINT_TO_POINTER (200));

  buffer = GTK_TEXT_BUFFER (palang_editor_get_document (palang_window_get_active_editor (fixture->window)));
  gtk_text_buffer_get_iter_at_mark (buffer, &cursor, gtk_text_buffer_get_insert (buffer));
  g_assert_cmpint (gtk_text_iter_get_line_offset (&cursor), ==, 2);

  /* Again, in the tab that is already open. */
  palang_window_open_file_at (fixture->window, file, 20, 1);
  wait_until (fixture->window, cursor_line_is_visible, GINT_TO_POINTER (20));
  g_assert_cmpuint (palang_window_get_n_editors (fixture->window), ==, 1);
}

static gboolean
search_has_finished (PalangWindow *window, gpointer data)
{
  PalangSearch *search = palang_search_panel_get_search (palang_window_get_search_panel (window));

  return !palang_search_is_running (search) &&
         g_list_model_get_n_items (palang_search_get_results (search)) > 0;
}

/* Ctrl+Shift+F searches the project folder; activating a result opens its
 * file at the match. */
static void
test_project_search (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) folder = g_file_new_for_path (fixture->dir);
  g_autoptr (GFile) file = write_file (fixture, "found.c", "a\nb\nint needle;\n");
  PalangSearchPanel *search_panel = palang_window_get_search_panel (fixture->window);
  PalangSearch *search = palang_search_panel_get_search (search_panel);
  g_autoptr (PalangSearchMatch) match = NULL;
  g_autofree char *rg = g_find_program_in_path ("rg");

  if (rg == NULL)
    {
      g_test_skip ("ripgrep (rg) is not installed");
      return;
    }

  show_window (fixture->window);
  palang_window_open_folder (fixture->window, folder);
  g_assert_true (g_file_equal (palang_search_panel_get_folder (search_panel), folder));

  /* Without a selection, Ctrl+Shift+F just shows the page. */
  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.search-project", NULL);
  g_assert_true (gtk_widget_get_visible (GTK_WIDGET (palang_window_get_panel (fixture->window))));
  g_assert_cmpstr (palang_panel_get_visible_page (palang_window_get_panel (fixture->window)), ==, "search");

  palang_search_panel_focus (search_panel, "needle");
  wait_until (fixture->window, search_has_finished, NULL);

  match = g_list_model_get_item (palang_search_get_results (search), 0);
  g_signal_emit_by_name (search_panel, "match-activated", match);
  wait_until (fixture->window, active_file_is, file);
  wait_until (fixture->window, cursor_line_is_visible, GINT_TO_POINTER (3));
}

static gboolean
quick_open_listed (PalangWindow *window, gpointer n)
{
  AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window));

  return PALANG_IS_PALETTE (dialog) &&
         g_list_model_get_n_items (palang_palette_get_matches (PALANG_PALETTE (dialog))) ==
         GPOINTER_TO_UINT (n);
}

/* Ctrl+P lists the folder's files; typing and Enter opens one. */
static void
test_quick_open (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) folder = g_file_new_for_path (fixture->dir);
  g_autoptr (GFile) wanted = write_file (fixture, "palang-panel.c", "");
  g_autoptr (GFile) other = write_file (fixture, "palang-application.c", "");
  g_autofree char *rg = g_find_program_in_path ("rg");
  AdwDialog *dialog;
  GtkWidget *entry = NULL;

  if (rg == NULL)
    {
      g_test_skip ("ripgrep (rg) is not installed");
      return;
    }

  show_window (fixture->window);
  palang_window_open_folder (fixture->window, folder);
  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.quick-open", NULL);
  wait_until (fixture->window, quick_open_listed, GUINT_TO_POINTER (2));

  dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (fixture->window));
  g_object_get (dialog, "focus-widget", &entry, NULL);
  gtk_editable_set_text (GTK_EDITABLE (entry), "pan");
  wait_until (fixture->window, quick_open_listed, GUINT_TO_POINTER (2));
  run_main_loop_briefly ();  /* let the list re-sort */

  g_signal_emit_by_name (entry, "activate");
  g_object_unref (entry);
  wait_until (fixture->window, active_file_is, wanted);
}

/* Pressing Ctrl+P while Quick Open is open doesn't stack a second one. */
static void
test_quick_open_once (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) folder = g_file_new_for_path (fixture->dir);
  AdwApplicationWindow *window = ADW_APPLICATION_WINDOW (fixture->window);
  AdwDialog *first;

  show_window (fixture->window);
  palang_window_open_folder (fixture->window, folder);

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.quick-open", NULL);
  first = adw_application_window_get_visible_dialog (window);
  g_assert_true (PALANG_IS_PALETTE (first));

  gtk_widget_activate_action (GTK_WIDGET (fixture->window), "win.quick-open", NULL);
  run_main_loop_briefly ();
  g_assert_true (adw_application_window_get_visible_dialog (window) == first);
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  adw_init ();
  gtk_source_init ();

  /* Without animations, dialogs open and close instantly, which keeps the
   * tests fast and less timing-dependent. */
  g_object_set (gtk_settings_get_default (), "gtk-enable-animations", FALSE, NULL);

  /* The tests open files like a.c; without this they would start the real
   * clangd (if installed). Language servers are tested in test-lsp.c with
   * a fake server. (The settings live in build/test-home, not the user's.) */
  g_assert_true (palang_settings_set_from_string (palang_settings_get_default (),
                                                  "c-server", "", NULL));
  g_assert_true (palang_settings_set_from_string (palang_settings_get_default (),
                                                  "python-server", "", NULL));
  g_assert_true (palang_settings_set_from_string (palang_settings_get_default (),
                                                  "javascript-server", "", NULL));

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD_TEST ("/window/starts-empty", test_starts_empty);
  ADD_TEST ("/window/new-document", test_new_document);
  ADD_TEST ("/window/open-replaces-blank-untitled", test_open_replaces_blank_untitled);
  ADD_TEST ("/window/open-twice-reuses-tab", test_open_twice_reuses_tab);
  ADD_TEST ("/window/edit-and-save", test_edit_and_save);
  ADD_TEST ("/window/close-unmodified-tab", test_close_unmodified_tab);
  ADD_TEST ("/window/open-missing-file", test_open_missing_file);
  ADD_TEST ("/window/close-modified-tab-cancel-then-discard",
            test_close_modified_tab_cancel_then_discard);
  ADD_TEST ("/window/close-modified-tab-save", test_close_modified_tab_save);
  ADD_TEST ("/window/close-window-with-changes", test_close_window_with_changes);
  ADD_TEST ("/window/reloads-unmodified-file", test_reloads_unmodified_file);
  ADD_TEST ("/window/modified-file-shows-banner", test_modified_file_shows_banner);
  ADD_TEST ("/window/deleted-file-shows-banner", test_deleted_file_shows_banner);
  ADD_TEST ("/window/dup-session", test_dup_session);
  ADD_TEST ("/window/restore-session", test_restore_session);
  ADD_TEST ("/window/toggle-panel", test_toggle_panel);
  ADD_TEST ("/window/panel-session", test_panel_session);
  ADD_TEST ("/window/active-editor-property", test_active_editor_property);
  ADD_TEST ("/window/terminal-follows-folder", test_terminal_follows_folder);
  ADD_TEST ("/window/open-file-at", test_open_file_at);
  ADD_TEST ("/window/project-search", test_project_search);
  ADD_TEST ("/window/quick-open", test_quick_open);
  ADD_TEST ("/window/quick-open-once", test_quick_open_once);

#undef ADD_TEST

  return g_test_run ();
}
