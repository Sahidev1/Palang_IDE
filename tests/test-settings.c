/* test-settings.c — tests for PalangSettings (config.ini) and the
 * appearance decisions (color scheme, font CSS). No display needed.
 */

#include "config.h"

#include <string.h>

#include <glib/gstdio.h>

#include "palang-appearance.h"
#include "palang-settings.h"

/* ---- Helpers ------------------------------------------------------------ */

typedef struct {
  char *dir;
  char *path;  /* dir/config.ini */
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", NULL);
  fixture->path = g_build_filename (fixture->dir, "config.ini", NULL);
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
write_config (Fixture *fixture, const char *contents)
{
  g_autoptr (GError) error = NULL;

  g_file_set_contents (fixture->path, contents, -1, &error);
  g_assert_no_error (error);
}

static char *
read_config (Fixture *fixture)
{
  char *contents = NULL;

  g_file_get_contents (fixture->path, &contents, NULL, NULL);
  return contents;
}

static void
count_notify (GObject *object, GParamSpec *pspec, guint *count)
{
  (*count)++;
}

/* ---- Settings ----------------------------------------------------------- */

/* No file yet: defaults, and nothing is written until something changes. */
static void
test_defaults (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = palang_settings_new_for_path (fixture->path);
  gboolean insert_spaces, line_numbers, wrap;

  g_object_get (settings,
                "insert-spaces", &insert_spaces,
                "show-line-numbers", &line_numbers,
                "wrap-lines", &wrap,
                NULL);

  g_assert_cmpstr (palang_settings_get_color_scheme (settings), ==, "dark");
  g_assert_cmpstr (palang_settings_get_font (settings), ==, "");
  g_assert_cmpuint (palang_settings_get_tab_width (settings), ==, 4);
  g_assert_true (insert_spaces);
  g_assert_true (line_numbers);
  g_assert_false (wrap);
  g_assert_false (g_file_test (fixture->path, G_FILE_TEST_EXISTS));
}

/* Changes are saved immediately and read back by a new instance. */
static void
test_save_and_reload (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) first = palang_settings_new_for_path (fixture->path);
  g_autoptr (PalangSettings) second = NULL;
  gboolean wrap;

  g_object_set (first,
                "color-scheme", "light",
                "font", "JetBrains Mono 12",
                "tab-width", 2,
                "wrap-lines", TRUE,
                NULL);

  second = palang_settings_new_for_path (fixture->path);
  g_object_get (second, "wrap-lines", &wrap, NULL);

  g_assert_cmpstr (palang_settings_get_color_scheme (second), ==, "light");
  g_assert_cmpstr (palang_settings_get_font (second), ==, "JetBrains Mono 12");
  g_assert_cmpuint (palang_settings_get_tab_width (second), ==, 2);
  g_assert_true (wrap);
}

/* Groups, keys and comments we don't know about survive a rewrite. */
static void
test_keeps_unknown_content (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = NULL;
  g_autofree char *contents = NULL;

  write_config (fixture,
                "# my notes\n"
                "[editor]\n"
                "tab-width=8\n"
                "future-option=yes\n"
                "\n"
                "[ai.completion]\n"
                "provider=ollama\n");

  settings = palang_settings_new_for_path (fixture->path);
  g_assert_cmpuint (palang_settings_get_tab_width (settings), ==, 8);

  g_object_set (settings, "tab-width", 3, NULL);
  contents = read_config (fixture);

  g_assert_nonnull (strstr (contents, "# my notes"));
  g_assert_nonnull (strstr (contents, "tab-width=3"));
  g_assert_nonnull (strstr (contents, "future-option=yes"));
  g_assert_nonnull (strstr (contents, "[ai.completion]"));
  g_assert_nonnull (strstr (contents, "provider=ollama"));
}

/* Invalid values in the file fall back to the defaults. */
static void
test_invalid_values_use_defaults (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = NULL;
  gboolean insert_spaces;

  write_config (fixture,
                "[appearance]\n"
                "color-scheme=purple\n"
                "[editor]\n"
                "tab-width=0\n"
                "insert-spaces=maybe\n");

  settings = palang_settings_new_for_path (fixture->path);
  g_object_get (settings, "insert-spaces", &insert_spaces, NULL);

  g_assert_cmpstr (palang_settings_get_color_scheme (settings), ==, "dark");
  g_assert_cmpuint (palang_settings_get_tab_width (settings), ==, 4);
  g_assert_true (insert_spaces);
}

/* Setting an invalid color scheme is refused with a warning. */
static void
test_rejects_invalid_color_scheme (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = palang_settings_new_for_path (fixture->path);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*Invalid value*");
  g_object_set (settings, "color-scheme", "purple", NULL);
  g_test_assert_expected_messages ();

  g_assert_cmpstr (palang_settings_get_color_scheme (settings), ==, "dark");
}

/* "notify" fires only when a value really changes. */
static void
test_notifies_only_on_change (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = palang_settings_new_for_path (fixture->path);
  guint count = 0;

  g_signal_connect (settings, "notify::tab-width", G_CALLBACK (count_notify), &count);

  g_object_set (settings, "tab-width", 4, NULL);  /* already 4 */
  g_assert_cmpuint (count, ==, 0);

  g_object_set (settings, "tab-width", 6, NULL);
  g_assert_cmpuint (count, ==, 1);
}

/* ---- Appearance --------------------------------------------------------- */

/* ---- Settings by name, as text ----------------------------------------- */

static void
test_by_name (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = palang_settings_new_for_path (fixture->path);
  g_autoptr (PalangSettings) reloaded = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *value = NULL;
  guint notifications = 0;

  g_signal_connect (settings, "notify::tab-width", G_CALLBACK (count_notify), &notifications);

  /* Goes through the property: notified and saved. */
  g_assert_true (palang_settings_set_from_string (settings, "tab-width", "2", &error));
  g_assert_no_error (error);
  g_assert_cmpuint (palang_settings_get_tab_width (settings), ==, 2);
  g_assert_cmpuint (notifications, ==, 1);
  reloaded = palang_settings_new_for_path (fixture->path);
  g_assert_cmpuint (palang_settings_get_tab_width (reloaded), ==, 2);

  g_assert_true (palang_settings_set_from_string (settings, "wrap-lines", "true", NULL));
  value = palang_settings_dup_value (settings, "wrap-lines");
  g_assert_cmpstr (value, ==, "true");
  g_clear_pointer (&value, g_free);

  g_assert_true (palang_settings_set_from_string (settings, "font", "Monospace 12", NULL));
  value = palang_settings_dup_value (settings, "font");
  g_assert_cmpstr (value, ==, "Monospace 12");
  g_clear_pointer (&value, g_free);

  g_assert_true (palang_settings_reset (settings, "tab-width", NULL));
  g_assert_cmpuint (palang_settings_get_tab_width (settings), ==, 4);
}

static void
test_by_name_rejects (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = palang_settings_new_for_path (fixture->path);
  const char *bad[][2] = {
    { "tab-width", "0" },        /* below the minimum */
    { "tab-width", "17" },       /* above the maximum */
    { "tab-width", "2x" },       /* not a number */
    { "tab-width", "-1" },
    { "wrap-lines", "yes" },     /* booleans are true/false */
    { "color-scheme", "purple" },
  };

  for (guint i = 0; i < G_N_ELEMENTS (bad); i++)
    {
      g_autoptr (GError) error = NULL;

      g_assert_false (palang_settings_set_from_string (settings, bad[i][0], bad[i][1], &error));
      g_assert_error (error, PALANG_SETTINGS_ERROR, PALANG_SETTINGS_ERROR_INVALID_VALUE);
    }

  {
    g_autoptr (GError) error = NULL;

    g_assert_false (palang_settings_set_from_string (settings, "nope", "1", &error));
    g_assert_error (error, PALANG_SETTINGS_ERROR, PALANG_SETTINGS_ERROR_UNKNOWN_NAME);
    g_assert_null (palang_settings_dup_value (settings, "nope"));
  }

  /* Nothing changed. */
  g_assert_cmpuint (palang_settings_get_tab_width (settings), ==, 4);
  g_assert_cmpstr (palang_settings_get_color_scheme (settings), ==, "dark");
}

/* Every setting has a description and an allowed-values phrase. */
static void
test_describes_every_setting (void)
{
  g_auto (GStrv) names = palang_settings_dup_names ();
  g_auto (GStrv) choices = palang_settings_dup_choices ("color-scheme");
  g_autofree char *allowed = palang_settings_dup_allowed_values ("tab-width");

  g_assert_cmpuint (g_strv_length (names), ==, 10);  /* 7 + 3 language servers */
  for (guint i = 0; names[i] != NULL; i++)
    {
      g_autofree char *phrase = palang_settings_dup_allowed_values (names[i]);
      g_autofree char *default_value = palang_settings_dup_default_value (names[i]);

      g_assert_nonnull (palang_settings_get_description (names[i]));
      g_assert_nonnull (phrase);
      g_assert_nonnull (default_value);
    }

  g_assert_cmpstrv (choices, ((const char *[]) { "system", "light", "dark", NULL }));
  g_assert_cmpstr (allowed, ==, "a whole number from 1 to 16");
  g_assert_null (palang_settings_dup_choices ("font"));
}

static void
test_choose_color_scheme (void)
{
  struct {
    const char    *setting;
    gboolean       system_dark;
    const char    *theme;
    AdwColorScheme expected;
  } cases[] = {
    { "light",  TRUE,  "Mint-Y-Dark", ADW_COLOR_SCHEME_FORCE_LIGHT },
    { "dark",   FALSE, "Adwaita",     ADW_COLOR_SCHEME_FORCE_DARK },
    /* System preference reported: follow it. */
    { "system", TRUE,  "Adwaita",     ADW_COLOR_SCHEME_DEFAULT },
    /* No preference reported (MATE): use the theme name. */
    { "system", FALSE, "Mint-Y-Dark-Sand", ADW_COLOR_SCHEME_FORCE_DARK },
    { "system", FALSE, "BlackMATE-DARK",   ADW_COLOR_SCHEME_FORCE_DARK },
    { "system", FALSE, "Windows XP Luna",  ADW_COLOR_SCHEME_DEFAULT },
    { "system", FALSE, NULL,               ADW_COLOR_SCHEME_DEFAULT },
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    g_assert_cmpint (palang_appearance_choose_color_scheme (cases[i].setting,
                                                            cases[i].system_dark,
                                                            cases[i].theme),
                     ==, cases[i].expected);
}

static void
test_font_to_css (void)
{
  struct {
    const char *font;
    const char *css;
  } cases[] = {
    { "",                   "" },
    { "Monospace 11",       ".palang-editor { font-family: \"Monospace\"; font-size: 11pt; }" },
    { "JetBrains Mono 10.5", ".palang-editor { font-family: \"JetBrains Mono\"; font-size: 10.5pt; }" },
    { "Fira Code Bold 12",  ".palang-editor { font-family: \"Fira Code\"; font-size: 12pt; font-weight: 700; }" },
    { "Hack Italic 9",      ".palang-editor { font-family: \"Hack\"; font-size: 9pt; font-style: italic; }" },
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autofree char *css = palang_appearance_font_to_css (cases[i].font);
      g_assert_cmpstr (css, ==, cases[i].css);
    }
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD_TEST ("/settings/defaults", test_defaults);
  ADD_TEST ("/settings/save-and-reload", test_save_and_reload);
  ADD_TEST ("/settings/keeps-unknown-content", test_keeps_unknown_content);
  ADD_TEST ("/settings/invalid-values-use-defaults", test_invalid_values_use_defaults);
  ADD_TEST ("/settings/rejects-invalid-color-scheme", test_rejects_invalid_color_scheme);
  ADD_TEST ("/settings/notifies-only-on-change", test_notifies_only_on_change);
  ADD_TEST ("/settings/by-name", test_by_name);
  ADD_TEST ("/settings/by-name-rejects", test_by_name_rejects);
  g_test_add_func ("/settings/describes-every-setting", test_describes_every_setting);
  g_test_add_func ("/appearance/choose-color-scheme", test_choose_color_scheme);
  g_test_add_func ("/appearance/font-to-css", test_font_to_css);

#undef ADD_TEST

  return g_test_run ();
}
