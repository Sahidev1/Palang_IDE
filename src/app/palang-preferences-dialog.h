/* palang-preferences-dialog.h — the Preferences window (Ctrl+,).
 *
 * Every row is bound to a PalangSettings property, so changes apply (and
 * are saved) immediately; there is no OK/Apply button.
 */

#pragma once

#include <adwaita.h>

#include "palang-settings.h"

G_BEGIN_DECLS

#define PALANG_TYPE_PREFERENCES_DIALOG (palang_preferences_dialog_get_type ())

G_DECLARE_FINAL_TYPE (PalangPreferencesDialog, palang_preferences_dialog,
                      PALANG, PREFERENCES_DIALOG, AdwPreferencesDialog)

AdwDialog *palang_preferences_dialog_new (PalangSettings *settings);

G_END_DECLS
