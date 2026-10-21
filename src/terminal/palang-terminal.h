/* palang-terminal.h — the integrated terminal.
 *
 * A PalangTerminal runs the user's shell in a VTE terminal widget (the one
 * GNOME Terminal uses). The shell starts the first time the terminal is
 * shown, in the folder set with palang_terminal_set_directory () (the home
 * folder if none). When the shell exits, the terminal says so and Enter
 * starts a new one.
 *
 * Ctrl+Shift+C / Ctrl+Shift+V copy and paste. Colors follow light/dark and
 * the font follows the editor font setting.
 */

#pragma once

#include <adwaita.h>
#include <vte/vte.h>

G_BEGIN_DECLS

#define PALANG_TYPE_TERMINAL (palang_terminal_get_type ())

G_DECLARE_FINAL_TYPE (PalangTerminal, palang_terminal, PALANG, TERMINAL, AdwBin)

PalangTerminal *palang_terminal_new             (void);

/* @directory: (nullable): where the next shell starts; NULL for the home
 * folder. A shell that is already running stays where it is. Also the
 * "directory" property. */
void            palang_terminal_set_directory   (PalangTerminal *self,
                                                 GFile          *directory);

/* Returns: (transfer none) (nullable) */
GFile          *palang_terminal_get_directory   (PalangTerminal *self);

/* @environment: (nullable): "NAME=value" strings added to (or replacing in)
 * the environment the next shell gets; the rest is inherited from Palang. */
void            palang_terminal_set_environment (PalangTerminal     *self,
                                                 const char * const *environment);

/* Starts the shell unless one is running or starting. Showing the
 * terminal calls this; there's usually no need to call it yourself. */
void            palang_terminal_start           (PalangTerminal *self);

/* TRUE from the moment the shell has started until it exits. */
gboolean        palang_terminal_is_running      (PalangTerminal *self);

/* Hands the key event @controller is handling to the terminal, as if it
 * had arrived there normally. For a window whose own shortcuts would
 * otherwise take keys the shell needs (Ctrl+W deletes a word in bash): its
 * key controller can give them to the terminal first. Call it only from a
 * "key-pressed" handler.
 * Returns: TRUE if the terminal used the key. */
gboolean        palang_terminal_forward_key     (PalangTerminal        *self,
                                                 GtkEventControllerKey *controller);

/* Returns: (transfer none): the VTE widget inside, e.g. to send it input
 * (vte_terminal_feed_child ()) or read its text. */
VteTerminal    *palang_terminal_get_vte         (PalangTerminal *self);

G_END_DECLS
