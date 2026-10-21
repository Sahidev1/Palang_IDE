/* test-command-line.c — the `palang` subcommands (help, settings, get,
 * set, reset, __complete). No display needed: the replies are text.
 *
 * Which arguments count as a subcommand (only in the Palang terminal) is
 * decided by the application and checked in the real app; see
 * MANUAL_TESTS.md, Phase 2a.
 */

#include "config.h"

#include <string.h>

#include <glib/gstdio.h>

#include "palang-command-line.h"

typedef struct {
  char           *dir;
  char           *path;      /* dir/config.ini */
  PalangSettings *settings;
  GString        *output;
  GString        *errors;
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", NULL);
  fixture->path = g_build_filename (fixture->dir, "config.ini", NULL);
  fixture->settings = palang_settings_new_for_path (fixture->path);
  fixture->output = g_string_new (NULL);
  fixture->errors = g_string_new (NULL);
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_clear_object (&fixture->settings);
  g_string_free (fixture->output, TRUE);
  g_string_free (fixture->errors, TRUE);
  g_unlink (fixture->path);
  g_rmdir (fixture->dir);
  g_free (fixture->path);
  g_free (fixture->dir);
}

/* Runs "palang <words…>" (a NULL-terminated list) after clearing the
 * previous output. Returns: the exit status. */
static int
run (Fixture    *fixture,
     const char *first_word,
     ...)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_auto (GStrv) args = NULL;
  const char *word;
  va_list words;

  g_string_truncate (fixture->output, 0);
  g_string_truncate (fixture->errors, 0);

  g_strv_builder_add (builder, first_word);
  va_start (words, first_word);
  while ((word = va_arg (words, const char *)) != NULL)
    g_strv_builder_add (builder, word);
  va_end (words);

  args = g_strv_builder_end (builder);
  return palang_command_line_run (fixture->settings, (const char * const *) args,
                                  fixture->output, fixture->errors);
}

static gboolean
contains (GString    *text,
          const char *part)
{
  return strstr (text->str, part) != NULL;
}

/* ---- Tests -------------------------------------------------------------- */

static void
test_is_subcommand (void)
{
  g_assert_true (palang_command_line_is_subcommand ("set"));
  g_assert_true (palang_command_line_is_subcommand ("settings"));
  g_assert_false (palang_command_line_is_subcommand ("./set"));
  g_assert_false (palang_command_line_is_subcommand ("main.c"));
  g_assert_false (palang_command_line_is_subcommand (NULL));
}

static void
test_help (Fixture *fixture, gconstpointer data)
{
  g_assert_cmpint (run (fixture, "help", NULL), ==, 0);

  g_assert_true (contains (fixture->output, "set NAME VALUE"));
  g_assert_true (contains (fixture->output, "tab-width"));      /* settings listed */
  g_assert_false (contains (fixture->output, "__complete"));   /* hidden */
  g_assert_cmpuint (fixture->errors->len, ==, 0);
}

/* `settings` lists every setting from the settings table. */
static void
test_settings (Fixture *fixture, gconstpointer data)
{
  g_auto (GStrv) names = palang_settings_dup_names ();

  g_assert_cmpint (run (fixture, "settings", NULL), ==, 0);

  for (guint i = 0; names[i] != NULL; i++)
    {
      g_autofree char *line = g_strdup_printf ("%s = ", names[i]);
      g_assert_true (contains (fixture->output, line));
    }
  g_assert_true (contains (fixture->output, "tab-width = 4\n"));
  g_assert_true (contains (fixture->output, "font = \"\"\n"));
  g_assert_true (contains (fixture->output, "Allowed: system, light, dark. Default: dark."));
}

static void
test_get_set_reset (Fixture *fixture, gconstpointer data)
{
  g_assert_cmpint (run (fixture, "get", "tab-width", NULL), ==, 0);
  g_assert_cmpstr (fixture->output->str, ==, "4\n");

  g_assert_cmpint (run (fixture, "set", "tab-width", "2", NULL), ==, 0);
  g_assert_cmpstr (fixture->output->str, ==, "tab-width = 2\n");
  g_assert_cmpuint (palang_settings_get_tab_width (fixture->settings), ==, 2);

  /* Several words make one value. */
  g_assert_cmpint (run (fixture, "set", "font", "Monospace", "12", NULL), ==, 0);
  g_assert_cmpstr (palang_settings_get_font (fixture->settings), ==, "Monospace 12");

  g_assert_cmpint (run (fixture, "reset", "tab-width", NULL), ==, 0);
  g_assert_cmpstr (fixture->output->str, ==, "tab-width = 4 (the default)\n");
  g_assert_cmpuint (palang_settings_get_tab_width (fixture->settings), ==, 4);
}

static void
test_errors (Fixture *fixture, gconstpointer data)
{
  g_assert_cmpint (run (fixture, "set", "color-scheme", "purple", NULL), ==, 1);
  g_assert_true (contains (fixture->errors, "Allowed: system, light, dark."));
  g_assert_cmpuint (fixture->output->len, ==, 0);
  g_assert_cmpstr (palang_settings_get_color_scheme (fixture->settings), ==, "dark");

  g_assert_cmpint (run (fixture, "get", "nope", NULL), ==, 1);
  g_assert_true (contains (fixture->errors, "no setting called “nope”"));
  g_assert_true (contains (fixture->errors, "palang settings"));

  /* Wrong number of words: usage. */
  g_assert_cmpint (run (fixture, "set", "tab-width", NULL), ==, 1);
  g_assert_cmpstr (fixture->errors->str, ==, "Usage: palang set NAME VALUE\n");
  g_assert_cmpint (run (fixture, "get", NULL), ==, 1);
  g_assert_cmpint (run (fixture, "reset", "a", "b", NULL), ==, 1);
}

/* The words the bash completion script offers. */
static void
test_complete (Fixture *fixture, gconstpointer data)
{
  g_assert_cmpint (run (fixture, "__complete", "subcommands", NULL), ==, 0);
  g_assert_cmpstr (fixture->output->str, ==, "help\nsettings\nget\nset\nreset\n");

  g_assert_cmpint (run (fixture, "__complete", "names", NULL), ==, 0);
  g_assert_true (g_str_has_prefix (fixture->output->str, "color-scheme\nfont\ntab-width\n"));

  g_assert_cmpint (run (fixture, "__complete", "values", "color-scheme", NULL), ==, 0);
  g_assert_cmpstr (fixture->output->str, ==, "system\nlight\ndark\n");

  g_assert_cmpint (run (fixture, "__complete", "values", "tab-width", NULL), ==, 0);
  g_assert_cmpstr (fixture->output->str, ==, "");
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  g_test_add_func ("/command-line/is-subcommand", test_is_subcommand);
  ADD_TEST ("/command-line/help", test_help);
  ADD_TEST ("/command-line/settings", test_settings);
  ADD_TEST ("/command-line/get-set-reset", test_get_set_reset);
  ADD_TEST ("/command-line/errors", test_errors);
  ADD_TEST ("/command-line/complete", test_complete);

#undef ADD_TEST

  return g_test_run ();
}
