/* palang-palette.h — the palette: type a few letters, pick from a list.
 *
 * Used for the command palette (Ctrl+Shift+P) and Quick Open (Ctrl+P). A
 * PalangPalette is a dialog with a search box over a list of
 * PalangPaletteItem; typing filters and sorts the list with fuzzy matching
 * ("sva" finds "Save All"). Up/Down choose, Enter or a click picks, Escape
 * closes. It doesn't know what the items mean: picking one closes the
 * dialog and emits "item-activated", and the window does the rest.
 */

#pragma once

#include <adwaita.h>

#include "palang-palette-item.h"

G_BEGIN_DECLS

#define PALANG_TYPE_PALETTE (palang_palette_get_type ())

G_DECLARE_FINAL_TYPE (PalangPalette, palang_palette, PALANG, PALETTE, AdwDialog)

/* @placeholder: shown in the empty search box, e.g. "Run a command". */
PalangPalette *palang_palette_new         (const char *placeholder);

/* @items: a list of PalangPaletteItem, in the order to show them while
 * nothing is typed. Can be set (again) while the palette is open, e.g.
 * once a list of files has loaded. */
void           palang_palette_set_items   (PalangPalette *self,
                                           GListModel    *items);

/* @status: (nullable): a short note above the list ("Listing files…", an
 * error), or NULL for none. */
void           palang_palette_set_status  (PalangPalette *self,
                                           const char    *status);

/* Returns: (transfer none): the items shown now (matching what is typed,
 * best first), a list of PalangPaletteItem. */
GListModel    *palang_palette_get_matches (PalangPalette *self);

/* Signal "item-activated" (PalangPaletteItem *item): the user picked
 * @item. Emitted after the palette has closed. */

G_END_DECLS
