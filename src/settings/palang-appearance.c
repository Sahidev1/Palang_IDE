/* palang-appearance.c — color scheme and editor font.
 *
 * Color scheme: libadwaita's AdwStyleManager decides light or dark. Its
 * DEFAULT mode follows the desktop's dark-mode preference, which some
 * desktops (MATE) never report; there we look at the GTK theme name.
 *
 * Font: every editor's text view has the CSS class "palang-editor"; one
 * CSS provider for the whole display sets that class's font.
 */

#include "config.h"

#include <string.h>

#include "palang-appearance.h"

AdwColorScheme
palang_appearance_choose_color_scheme (const char *setting,
                                       gboolean    system_prefers_dark,
                                       const char *gtk_theme_name)
{
  g_autofree char *theme_lower = NULL;

  if (g_strcmp0 (setting, "light") == 0)
    return ADW_COLOR_SCHEME_FORCE_LIGHT;

  if (g_strcmp0 (setting, "dark") == 0)
    return ADW_COLOR_SCHEME_FORCE_DARK;

  /* "system": follow the desktop's preference when it has one. */
  if (system_prefers_dark)
    return ADW_COLOR_SCHEME_DEFAULT;

  theme_lower = gtk_theme_name != NULL ? g_ascii_strdown (gtk_theme_name, -1) : NULL;
  if (theme_lower != NULL && strstr (theme_lower, "dark") != NULL)
    return ADW_COLOR_SCHEME_FORCE_DARK;

  return ADW_COLOR_SCHEME_DEFAULT;
}

char *
palang_appearance_font_to_css (const char *font)
{
  g_autoptr (PangoFontDescription) description = NULL;
  g_autoptr (GString) css = NULL;
  PangoFontMask set;

  if (font == NULL || font[0] == '\0')
    return g_strdup ("");

  description = pango_font_description_from_string (font);
  set = pango_font_description_get_set_fields (description);
  css = g_string_new (".palang-editor {");

  if (set & PANGO_FONT_MASK_FAMILY)
    {
      /* Quote the family name; escape quotes and backslashes inside it. */
      g_autofree char *escaped = g_strescape (pango_font_description_get_family (description), NULL);
      g_string_append_printf (css, " font-family: \"%s\";", escaped);
    }

  if (set & PANGO_FONT_MASK_SIZE)
    {
      double size = (double) pango_font_description_get_size (description) / PANGO_SCALE;
      const char *unit = pango_font_description_get_size_is_absolute (description) ? "px" : "pt";
      char number[G_ASCII_DTOSTR_BUF_SIZE];

      /* g_ascii_dtostr always uses "." as the decimal point, whatever the
       * user's language (CSS requires it). */
      g_string_append_printf (css, " font-size: %s%s;",
                              g_ascii_dtostr (number, sizeof number, size), unit);
    }

  /* Pango reports weight and style as "set" even when they're just normal,
   * so only write them when they differ from normal. */
  if ((set & PANGO_FONT_MASK_WEIGHT) &&
      pango_font_description_get_weight (description) != PANGO_WEIGHT_NORMAL)
    g_string_append_printf (css, " font-weight: %d;",
                            (int) pango_font_description_get_weight (description));

  if ((set & PANGO_FONT_MASK_STYLE) &&
      pango_font_description_get_style (description) != PANGO_STYLE_NORMAL)
    g_string_append (css, " font-style: italic;");

  g_string_append (css, " }");
  return g_string_free (g_steal_pointer (&css), FALSE);
}

/* ---- Applying ----------------------------------------------------------- */

static GtkCssProvider *font_provider;  /* lives for the whole app */

static void
apply_color_scheme (PalangSettings *settings)
{
  AdwStyleManager *style_manager = adw_style_manager_get_default ();
  g_autofree char *gtk_theme_name = NULL;
  gboolean system_prefers_dark;

  g_object_get (gtk_settings_get_default (), "gtk-theme-name", &gtk_theme_name, NULL);

  /* To learn what the desktop prefers, briefly follow it and look. */
  adw_style_manager_set_color_scheme (style_manager, ADW_COLOR_SCHEME_DEFAULT);
  system_prefers_dark = adw_style_manager_get_dark (style_manager);

  adw_style_manager_set_color_scheme (style_manager,
                                      palang_appearance_choose_color_scheme (palang_settings_get_color_scheme (settings),
                                                                             system_prefers_dark,
                                                                             gtk_theme_name));
}

static void
apply_font (PalangSettings *settings)
{
  g_autofree char *css = palang_appearance_font_to_css (palang_settings_get_font (settings));

  gtk_css_provider_load_from_string (font_provider, css);
}

/* Shared handler for every change that can affect the color scheme. The
 * signals it's connected to pass different arguments; it uses none. */
static void
on_color_scheme_input_changed (GObject        *object,
                               GParamSpec     *pspec,
                               PalangSettings *settings)
{
  apply_color_scheme (settings);
}

static void
on_font_changed (PalangSettings *settings,
                 GParamSpec     *pspec,
                 gpointer        user_data)
{
  apply_font (settings);
}

void
palang_appearance_follow (PalangSettings *settings)
{
  g_return_if_fail (PALANG_IS_SETTINGS (settings));
  g_return_if_fail (font_provider == NULL);

  font_provider = gtk_css_provider_new ();
  /* APPLICATION priority beats the theme's monospace font. */
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (font_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_signal_connect (settings, "notify::color-scheme",
                    G_CALLBACK (on_color_scheme_input_changed), settings);
  g_signal_connect (gtk_settings_get_default (), "notify::gtk-theme-name",
                    G_CALLBACK (on_color_scheme_input_changed), settings);
  g_signal_connect (settings, "notify::font", G_CALLBACK (on_font_changed), NULL);

  apply_color_scheme (settings);
  apply_font (settings);
}
