/* palang-settings.h — the user's preferences, saved in config.ini.
 *
 * Each preference is a GObject property, so widgets can bind to it and
 * follow changes immediately (e.g. every editor's tab width). Any change is
 * written to the file right away.
 *
 * The file is ~/.config/palang/config.ini, in GKeyFile (INI) format:
 *
 *   [appearance]
 *   color-scheme=system        # system | light | dark
 *
 *   [editor]
 *   font=Monospace 11          # empty: the system's monospace font
 *   tab-width=4
 *   insert-spaces=true
 *   show-line-numbers=true
 *   highlight-current-line=true
 *   wrap-lines=false
 *
 * Unknown groups and keys are kept when the file is rewritten, and invalid
 * values fall back to the defaults.
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PALANG_TYPE_SETTINGS (palang_settings_get_type ())

G_DECLARE_FINAL_TYPE (PalangSettings, palang_settings, PALANG, SETTINGS, GObject)

/* Returns: (transfer none): the app-wide settings, loaded from the user's
 * config.ini on first use. */
PalangSettings *palang_settings_get_default (void);

/* Returns: (transfer full): settings loaded from (and saved to) @path. */
PalangSettings *palang_settings_new_for_path (const char *path);

const char     *palang_settings_get_color_scheme (PalangSettings *self);
const char     *palang_settings_get_font         (PalangSettings *self);
guint           palang_settings_get_tab_width    (PalangSettings *self);

G_END_DECLS
