/* palang-command.h — one entry of the command palette.
 *
 * A PalangCommand is a name a person reads ("Save All"), the action that
 * runs it ("win.save-all") and its keyboard shortcut ("<Control><Alt>s").
 * The application builds the list of commands (a GListModel of
 * PalangCommand) from its command table; the palette only reads that list,
 * so palette/ never needs to know about app/.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define PALANG_TYPE_COMMAND (palang_command_get_type ())

G_DECLARE_FINAL_TYPE (PalangCommand, palang_command, PALANG, COMMAND, GObject)

/* @accel: (nullable): a shortcut in gtk_accelerator_parse () format, or
 * NULL for a command without one. */
PalangCommand *palang_command_new             (const char *action_name,
                                               const char *title,
                                               const char *accel);

/* Returns: (transfer none): e.g. "win.save-all". */
const char    *palang_command_get_action_name (PalangCommand *self);

/* Returns: (transfer none): e.g. "Save All". */
const char    *palang_command_get_title       (PalangCommand *self);

/* Returns: (transfer none) (nullable): e.g. "<Control><Alt>s". */
const char    *palang_command_get_accel       (PalangCommand *self);

G_END_DECLS
