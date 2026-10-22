/* test-terminal.c — UI tests for PalangTerminal.
 *
 * The terminal runs /bin/sh here (through $SHELL), so the tests don't
 * depend on the user's shell or its startup files. They type commands with
 * vte_terminal_feed_child () and read the screen with
 * vte_terminal_get_text_format ().
 *
 * Keyboard handling (Ctrl+Shift+C/V, Enter to restart, which shortcuts
 * reach the shell) needs real key presses: MANUAL_TESTS.md, Phase 2a.
 */

#include "config.h"

#include <string.h>

#include <glib/gstdio.h>

#include "palang-terminal.h"

typedef gboolean (*Condition) (PalangTerminal *terminal, gpointer data);

/* Runs the main loop until @condition is TRUE, failing after 5 seconds. */
static void
wait_until (PalangTerminal *terminal,
            Condition       condition,
            gpointer        data)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  while (!condition (terminal, data))
    {
      if (g_get_monotonic_time () > deadline)
        {
          g_autofree char *text = vte_terminal_get_text_format (palang_terminal_get_vte (terminal),
                                                                VTE_FORMAT_TEXT);
          g_printerr ("Terminal text:\n%s\n", text);
          g_error ("Timed out waiting for a condition");
        }
      g_main_context_iteration (NULL, FALSE);
    }
}

static gboolean
is_running (PalangTerminal *terminal, gpointer data)
{
  return palang_terminal_is_running (terminal);
}

static gboolean
is_stopped (PalangTerminal *terminal, gpointer data)
{
  return !palang_terminal_is_running (terminal);
}

static gboolean
shows_text (PalangTerminal *terminal, gpointer text)
{
  g_autofree char *screen = vte_terminal_get_text_format (palang_terminal_get_vte (terminal),
                                                          VTE_FORMAT_TEXT);

  return screen != NULL && strstr (screen, text) != NULL;
}

/* Fails, printing the whole screen, unless the terminal shows @text. The
 * screen tells what went wrong when this fails only now and then. */
static void
assert_shows_text (PalangTerminal *terminal,
                   const char     *text)
{
  g_autofree char *screen = NULL;

  if (shows_text (terminal, (gpointer) text))
    return;

  screen = vte_terminal_get_text_format (palang_terminal_get_vte (terminal), VTE_FORMAT_TEXT);
  g_printerr ("Terminal text:\n%s\n", screen);
  g_error ("The terminal doesn't show “%s”", text);
}

static void
type_command (PalangTerminal *terminal,
              const char     *command)
{
  vte_terminal_feed_child (palang_terminal_get_vte (terminal), command, -1);
  vte_terminal_feed_child (palang_terminal_get_vte (terminal), "\n", 1);
}

typedef struct {
  char           *dir;
  GtkWindow      *window;
  PalangTerminal *terminal;  /* inside window */
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GError) error = NULL;

  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", &error);
  g_assert_no_error (error);

  fixture->window = g_object_ref_sink (GTK_WINDOW (gtk_window_new ()));
  fixture->terminal = palang_terminal_new ();
  gtk_window_set_child (fixture->window, GTK_WIDGET (fixture->terminal));
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  gtk_window_destroy (fixture->window);
  g_clear_object (&fixture->window);
  g_rmdir (fixture->dir);
  g_free (fixture->dir);
}

/* ---- Tests -------------------------------------------------------------- */

/* Nothing runs until the terminal is shown. */
static void
test_starts_when_shown (Fixture *fixture, gconstpointer data)
{
  g_assert_false (palang_terminal_is_running (fixture->terminal));

  gtk_window_present (fixture->window);
  wait_until (fixture->terminal, is_running, NULL);
}

/* The shell starts in the chosen folder with the extra environment. */
static void
test_directory_and_environment (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GFile) folder = g_file_new_for_path (fixture->dir);
  const char *environment[] = { "PALANG_TEST_VALUE=fortytwo", NULL };
  g_autofree char *expected_pwd = g_strdup_printf ("\n%s\n", fixture->dir);

  palang_terminal_set_directory (fixture->terminal, folder);
  palang_terminal_set_environment (fixture->terminal, environment);
  gtk_window_present (fixture->window);
  wait_until (fixture->terminal, is_running, NULL);

  type_command (fixture->terminal, "pwd; echo value=$PALANG_TEST_VALUE");
  wait_until (fixture->terminal, shows_text, "value=fortytwo");
  assert_shows_text (fixture->terminal, expected_pwd);
}

/* When the shell exits, the terminal says so; starting again works. */
static void
test_exit_and_restart (Fixture *fixture, gconstpointer data)
{
  gtk_window_present (fixture->window);
  wait_until (fixture->terminal, is_running, NULL);

  type_command (fixture->terminal, "exit");
  wait_until (fixture->terminal, is_stopped, NULL);
  wait_until (fixture->terminal, shows_text, "Process exited");

  palang_terminal_start (fixture->terminal);
  wait_until (fixture->terminal, is_running, NULL);
  /* "again-42" only appears in the output, not in the typed command. */
  type_command (fixture->terminal, "echo again-$((40 + 2))");
  wait_until (fixture->terminal, shows_text, "again-42");
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  adw_init ();

  /* A predictable shell for every test (see the top of the file). */
  g_setenv ("SHELL", "/bin/sh", TRUE);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD_TEST ("/terminal/starts-when-shown", test_starts_when_shown);
  ADD_TEST ("/terminal/directory-and-environment", test_directory_and_environment);
  ADD_TEST ("/terminal/exit-and-restart", test_exit_and_restart);

#undef ADD_TEST

  return g_test_run ();
}
