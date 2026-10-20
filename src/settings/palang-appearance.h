/* palang-appearance.h — apply the light/dark and font settings to the app.
 *
 * palang_appearance_follow() applies the current settings and keeps them
 * applied when they change. The two decisions it makes are separate pure
 * functions, so they can be tested without a display.
 */

#pragma once

#include <adwaita.h>

#include "palang-settings.h"

G_BEGIN_DECLS

/* Applies @settings' color scheme and editor font now and whenever they
 * (or the desktop theme) change. Call once at startup. */
void           palang_appearance_follow               (PalangSettings *settings);

/* Decides the libadwaita color scheme.
 *
 * @setting: "system", "light" or "dark".
 * @system_prefers_dark: whether the desktop asks apps for dark mode.
 * @gtk_theme_name: (nullable): the desktop's GTK theme, e.g. "Mint-Y-Dark".
 *
 * In "system" mode, desktops that don't report a dark-mode preference (such
 * as MATE) still get dark mode when their GTK theme's name contains "dark". */
AdwColorScheme palang_appearance_choose_color_scheme  (const char *setting,
                                                       gboolean    system_prefers_dark,
                                                       const char *gtk_theme_name);

/* Returns: (transfer full): CSS that sets the editor font described by
 * @font (Pango format, e.g. "JetBrains Mono Bold 12"), or "" for an empty
 * @font (use the system's monospace font). */
char          *palang_appearance_font_to_css          (const char *font);

G_END_DECLS
