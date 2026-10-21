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

/* ---- Settings by name, as text ------------------------------------------
 *
 * For code that deals in names and text rather than properties, like the
 * `palang` command in the terminal ("palang set tab-width 2"). Values are
 * checked the same way as in Preferences. Booleans are "true" / "false";
 * numbers are written in decimal. Functions that take a @name return NULL
 * (or fail with PALANG_SETTINGS_ERROR_UNKNOWN_NAME) for unknown names. */

#define PALANG_SETTINGS_ERROR (palang_settings_error_quark ())
GQuark palang_settings_error_quark (void);

typedef enum {
  PALANG_SETTINGS_ERROR_UNKNOWN_NAME,
  PALANG_SETTINGS_ERROR_INVALID_VALUE,
} PalangSettingsError;

/* Returns: (transfer full): every setting's name, in a sensible order. */
char          **palang_settings_dup_names          (void);

/* Returns: (transfer none) (nullable): one sentence saying what @name does. */
const char     *palang_settings_get_description    (const char *name);

/* Returns: (transfer full) (nullable): the values @name accepts, when it is
 * a fixed list ("system", "light", "dark"; "true", "false"), else NULL. */
char          **palang_settings_dup_choices        (const char *name);

/* Returns: (transfer full) (nullable): the accepted values as a phrase for
 * people: "system, light, dark", "a whole number from 1 to 16", … */
char           *palang_settings_dup_allowed_values (const char *name);

/* Returns: (transfer full) (nullable): the default value of @name. */
char           *palang_settings_dup_default_value  (const char *name);

/* Returns: (transfer full) (nullable): the current value of @name. */
char           *palang_settings_dup_value          (PalangSettings *self,
                                                    const char     *name);

/* Sets @name from text, e.g. ("tab-width", "2"): applied live and saved.
 * Returns: FALSE (with @error set, its message ready to show) if the name
 * is unknown or the value isn't allowed. */
gboolean        palang_settings_set_from_string    (PalangSettings  *self,
                                                    const char      *name,
                                                    const char      *value,
                                                    GError         **error);

/* Sets @name back to its default. */
gboolean        palang_settings_reset              (PalangSettings  *self,
                                                    const char      *name,
                                                    GError         **error);

G_END_DECLS
