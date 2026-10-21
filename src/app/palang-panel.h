/* palang-panel.h — the bottom panel under the tabs.
 *
 * A PalangPanel holds pages ("Terminal", "Search") and shows one at a time,
 * with a switcher and a close button on top. The window puts it under the
 * tabs in a vertical GtkPaned, so the user can drag its top edge; show and
 * hide it with gtk_widget_set_visible () (Ctrl+J in the window).
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define PALANG_TYPE_PANEL (palang_panel_get_type ())

G_DECLARE_FINAL_TYPE (PalangPanel, palang_panel, PALANG, PANEL, AdwBin)

PalangPanel *palang_panel_new              (void);

/* Adds @page, shown as @title with @icon_name in the switcher. @id names
 * the page for palang_panel_set_visible_page () and the session. */
void         palang_panel_add_page         (PalangPanel *self,
                                            GtkWidget   *page,
                                            const char  *id,
                                            const char  *title,
                                            const char  *icon_name);

/* Shows the page added as @id. Unknown ids are ignored, so a session saved
 * by another version can't break anything. */
void         palang_panel_set_visible_page (PalangPanel *self,
                                            const char  *id);

/* Returns: (transfer none) (nullable): the id of the page shown. */
const char  *palang_panel_get_visible_page (PalangPanel *self);

/* Moves keyboard focus into the page shown. */
void         palang_panel_focus_page       (PalangPanel *self);

G_END_DECLS
