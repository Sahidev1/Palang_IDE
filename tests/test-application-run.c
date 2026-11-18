/* test-application-run.c — the whole application, run like a user starts
 * it: what happens on a quit signal (Ctrl+C in the terminal Palang was
 * started from), and what a launch with a folder ("palang .") restores.
 *
 * Each test runs Palang in a child process of its own
 * (g_test_trap_subprocess ()): an application can run only once per
 * process, and a test that goes wrong can't take the others with it.
 *
 * Under D-Bus, Palang is single-instance: a second Palang hands its files
 * to the first one. So the tests use another application ID and
 * G_APPLICATION_NON_UNIQUE; otherwise they could talk to a Palang the user
 * has open. Sessions are written to a temporary state folder of this
 * program's own (other tests run at the same time).
 */

#include "config.h"

#include <signal.h>

#include <glib/gstdio.h>

#include "palang-application.h"
#include "palang-session.h"
#include "palang-window.h"

/* What a test does inside the running application, step by step. A step
 * returns TRUE when it's done; until then it's called again, every few
 * milliseconds. This way each step waits for a state, not for a fixed
 * time. */
typedef gboolean (*Step) (PalangWindow *window);

typedef struct {
  const Step *steps;  /* NULL-terminated */
  guint       next;
  gint64      deadline;
} Script;

static gboolean
run_script (gpointer user_data)
{
  PalangWindow *window = PALANG_WINDOW (user_data);
  Script *script = g_object_get_data (G_OBJECT (window), "script");

  if (g_get_monotonic_time () > script->deadline)
    g_error ("Timed out at step %u", script->next);

  if (script->steps[script->next] == NULL)
    return G_SOURCE_REMOVE;

  if (script->steps[script->next] (window))
    script->next++;

  return G_SOURCE_CONTINUE;
}

/* The application created its window: start the script on it. */
static void
on_window_added (GtkApplication *app,
                 GtkWindow      *window,
                 const Step     *steps)
{
  Script *script = g_new0 (Script, 1);

  script->steps = steps;
  script->deadline = g_get_monotonic_time () + 20 * G_USEC_PER_SEC;
  g_object_set_data_full (G_OBJECT (window), "script", script, g_free);

  /* The source holds a reference to the window, and lets it go when it
   * stops (the window is destroyed when the app quits). */
  g_timeout_add_full (G_PRIORITY_DEFAULT, 20, run_script,
                      g_object_ref (window), g_object_unref);
}

/* Runs Palang with @arguments (NULL-terminated, without the program
 * name) and @steps, and returns its exit status. */
static int
run_palang (const char * const *arguments,
            const Step         *steps)
{
  g_autoptr (PalangApplication) app = palang_application_new ();
  g_autoptr (GPtrArray) argv = g_ptr_array_new ();

  g_application_set_application_id (G_APPLICATION (app), APPLICATION_ID ".Test");
  g_application_set_flags (G_APPLICATION (app),
                           g_application_get_flags (G_APPLICATION (app)) |
                           G_APPLICATION_NON_UNIQUE);
  g_signal_connect (app, "window-added", G_CALLBACK (on_window_added), (gpointer) steps);

  g_ptr_array_add (argv, (char *) "palang");
  for (guint i = 0; arguments != NULL && arguments[i] != NULL; i++)
    g_ptr_array_add (argv, (char *) arguments[i]);
  g_ptr_array_add (argv, NULL);

  return g_application_run (G_APPLICATION (app), argv->len - 1, (char **) argv->pdata);
}

static void
write_session (gboolean    show_panel,
               int         panel_height,
               const char *folder)
{
  g_autoptr (PalangSession) session = palang_session_new ();
  g_autofree char *path = palang_session_default_path ();
  g_autoptr (GError) error = NULL;

  session->show_panel = show_panel;
  session->panel_height = panel_height;
  if (folder != NULL)
    session->folder = g_file_new_for_path (folder);

  palang_session_save (session, path, &error);
  g_assert_no_error (error);
}

static PalangSession *
read_session (void)
{
  g_autofree char *path = palang_session_default_path ();
  PalangSession *session = palang_session_load (path);

  g_assert_nonnull (session);
  return session;
}

/* ---- Steps -------------------------------------------------------------- */

static gboolean
window_is_mapped (PalangWindow *window)
{
  return gtk_widget_get_mapped (GTK_WIDGET (window));
}

static gboolean
show_panel (PalangWindow *window)
{
  gtk_widget_set_visible (GTK_WIDGET (palang_window_get_panel (window)), TRUE);
  return TRUE;
}

/* Within 2 pixels: the divider's handle takes a pixel of the space. */
static gboolean
panel_is_180_high (PalangWindow *window)
{
  PalangPanel *panel = palang_window_get_panel (window);

  return gtk_widget_get_visible (GTK_WIDGET (panel)) &&
         ABS (gtk_widget_get_height (GTK_WIDGET (panel)) - 180) <= 2;
}

static gboolean
send_sigterm (PalangWindow *window)
{
  raise (SIGTERM);
  return TRUE;
}

static gboolean
type_into_new_document (PalangWindow *window)
{
  PalangEditor *editor;
  GtkTextBuffer *buffer;
  GtkTextIter end;

  palang_window_new_document (window);
  editor = palang_window_get_active_editor (window);
  buffer = GTK_TEXT_BUFFER (palang_editor_get_document (editor));
  gtk_text_buffer_get_end_iter (buffer, &end);
  gtk_text_buffer_insert (buffer, &end, "unsaved work", -1);
  return TRUE;
}

static gboolean
delete_session_and_send_sigint (PalangWindow *window)
{
  g_autofree char *path = palang_session_default_path ();

  g_unlink (path);
  raise (SIGINT);
  return TRUE;
}

static gboolean
question_is_asked (PalangWindow *window)
{
  AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window));

  return ADW_IS_ALERT_DIALOG (dialog);
}

/* The session was saved before the question, and the work is still
 * there. Then the test ends Palang without answering. */
static gboolean
check_session_saved_and_close (PalangWindow *window)
{
  g_autofree char *path = palang_session_default_path ();
  PalangEditor *editor = palang_window_get_active_editor (window);

  g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
  g_assert_true (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (palang_editor_get_document (editor))));

  gtk_window_destroy (GTK_WINDOW (window));
  return TRUE;
}

static gboolean
close_window (PalangWindow *window)
{
  gtk_window_close (GTK_WINDOW (window));
  return TRUE;
}

/* ---- Tests -------------------------------------------------------------- */

/* SIGTERM quits Palang the normal way, so the session is saved (here: the
 * panel was shown after the start, and keeps its restored height). */
static void
test_sigterm_saves_session (void)
{
  static const Step steps[] = {
    window_is_mapped, show_panel, panel_is_180_high, send_sigterm, NULL
  };

  if (g_test_subprocess ())
    {
      g_autoptr (PalangSession) session = NULL;

      write_session (FALSE, 180, NULL);
      g_assert_cmpint (run_palang (NULL, steps), ==, 0);

      session = read_session ();
      g_assert_true (session->show_panel);
      g_assert_cmpint (session->panel_height, ==, 180);
      return;
    }

  g_test_trap_subprocess (NULL, 60 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_INHERIT_STDERR);
  g_test_trap_assert_passed ();
}

/* With unsaved changes, SIGINT saves the session and then asks, like
 * Ctrl+Q: the text is not thrown away. */
static void
test_sigint_asks_about_unsaved (void)
{
  static const Step steps[] = {
    window_is_mapped, type_into_new_document, delete_session_and_send_sigint,
    question_is_asked, check_session_saved_and_close, NULL
  };

  if (g_test_subprocess ())
    {
      g_assert_cmpint (run_palang (NULL, steps), ==, 0);
      return;
    }

  g_test_trap_subprocess (NULL, 60 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_INHERIT_STDERR);
  g_test_trap_assert_passed ();
}

/* "palang <folder>" opens that folder, not the session's, but the bottom
 * panel comes back as it was (shown, 180 pixels high). */
static void
test_open_folder_restores_panel (void)
{
  static const Step steps[] = { window_is_mapped, panel_is_180_high, close_window, NULL };

  if (g_test_subprocess ())
    {
      /* Any folder will do; this program's state folder is one. */
      const char *folder_path = g_getenv ("PALANG_TEST_RUN_STATE");
      g_autofree char *other = g_build_filename (folder_path, "other", NULL);
      const char *arguments[] = { folder_path, NULL };
      g_autoptr (PalangSession) session = NULL;
      g_autoptr (GFile) folder = g_file_new_for_path (folder_path);

      write_session (TRUE, 180, other);
      g_assert_cmpint (run_palang (arguments, steps), ==, 0);

      session = read_session ();
      g_assert_true (session->show_panel);
      g_assert_cmpint (session->panel_height, ==, 180);
      g_assert_true (g_file_equal (session->folder, folder));
      return;
    }

  g_test_trap_subprocess (NULL, 60 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_INHERIT_STDERR);
  g_test_trap_assert_passed ();
}

int
main (int   argc,
      char *argv[])
{
  g_autofree char *state_dir = NULL;
  int status;

  /* A state folder of our own, made once by the parent process; the child
   * processes inherit the environment variable. It must be set before
   * anything asks GLib for the folder (it's remembered after that). */
  if (g_getenv ("PALANG_TEST_RUN_STATE") == NULL)
    {
      g_autoptr (GError) error = NULL;

      state_dir = g_dir_make_tmp ("palang-test-run-XXXXXX", &error);
      g_assert_no_error (error);
      g_setenv ("PALANG_TEST_RUN_STATE", state_dir, TRUE);
    }
  g_setenv ("XDG_STATE_HOME", g_getenv ("PALANG_TEST_RUN_STATE"), TRUE);

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/application-run/sigterm-saves-session", test_sigterm_saves_session);
  g_test_add_func ("/application-run/sigint-asks-about-unsaved", test_sigint_asks_about_unsaved);
  g_test_add_func ("/application-run/open-folder-restores-panel", test_open_folder_restores_panel);

  status = g_test_run ();

  /* The parent made the folder, so it cleans up. */
  if (state_dir != NULL)
    {
      g_autofree char *palang_dir = g_build_filename (state_dir, "palang", NULL);
      g_autofree char *session = g_build_filename (palang_dir, "session.ini", NULL);

      g_unlink (session);
      g_rmdir (palang_dir);
      g_rmdir (state_dir);
    }

  return status;
}
