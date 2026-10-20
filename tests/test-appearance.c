/* test-appearance.c — the color scheme follows the settings and, in
 * "system" mode, the GTK theme name (for desktops like MATE that don't
 * report a dark-mode preference). Runs on a display; under xvfb-run no
 * desktop preference is reported, just like on MATE.
 */

#include "config.h"

#include <glib/gstdio.h>

#include "palang-appearance.h"

static void
set_gtk_theme (const char *name)
{
  g_object_set (gtk_settings_get_default (), "gtk-theme-name", name, NULL);
}

static gboolean
is_dark (void)
{
  return adw_style_manager_get_dark (adw_style_manager_get_default ());
}

static void
test_follows_settings_and_theme_name (void)
{
  g_autofree char *dir = g_dir_make_tmp ("palang-test-XXXXXX", NULL);
  g_autofree char *path = g_build_filename (dir, "config.ini", NULL);
  g_autoptr (PalangSettings) settings = palang_settings_new_for_path (path);

  /* This test needs a desktop that reports no dark-mode preference. */
  if (adw_style_manager_get_system_supports_color_schemes (adw_style_manager_get_default ()) &&
      adw_style_manager_get_dark (adw_style_manager_get_default ()))
    {
      g_test_skip ("The desktop prefers dark mode; run under xvfb-run");
      return;
    }

  set_gtk_theme ("Adwaita");
  palang_appearance_follow (settings);
  g_assert_false (is_dark ());

  /* "system" + a dark GTK theme name → dark, live. */
  set_gtk_theme ("Mint-Y-Dark-Sand");
  g_assert_true (is_dark ());

  set_gtk_theme ("Windows XP Luna");
  g_assert_false (is_dark ());

  /* An explicit choice wins over the theme name. */
  g_object_set (settings, "color-scheme", "dark", NULL);
  g_assert_true (is_dark ());

  set_gtk_theme ("Mint-Y-Dark");
  g_object_set (settings, "color-scheme", "light", NULL);
  g_assert_false (is_dark ());

  g_unlink (path);
  g_rmdir (dir);
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  adw_init ();

  g_test_add_func ("/appearance/follows-settings-and-theme-name",
                   test_follows_settings_and_theme_name);

  return g_test_run ();
}
