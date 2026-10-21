/* palang-application.h — the PalangApplication type.
 *
 * One PalangApplication exists per running Palang. It owns the app-wide
 * actions (like Quit), the command table (every command with its name and
 * shortcut) and creates windows.
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define PALANG_TYPE_APPLICATION (palang_application_get_type ())

/* Declares the PalangApplication struct, its class, cast macros like
 * PALANG_APPLICATION (obj), and g_autoptr support. "Final" means nothing
 * can subclass it. */
G_DECLARE_FINAL_TYPE (PalangApplication, palang_application,
                      PALANG, APPLICATION, AdwApplication)

PalangApplication *palang_application_new          (void);

/* Returns: (transfer none): every command of the app, a list of
 * PalangCommand (see palang-command.h), for the command palette. */
GListModel        *palang_application_get_commands (PalangApplication *self);

G_END_DECLS
