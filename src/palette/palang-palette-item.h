/* palang-palette-item.h — one choice in the palette.
 *
 * What the palette shows (a title and a smaller subtitle), the text the
 * typed letters are matched against, and an id that tells the code which
 * choice was made: an action name for commands, a file's relative path
 * for Quick Open.
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PALANG_TYPE_PALETTE_ITEM (palang_palette_item_get_type ())

G_DECLARE_FINAL_TYPE (PalangPaletteItem, palang_palette_item, PALANG, PALETTE_ITEM, GObject)

/* @subtitle: (nullable): e.g. "Ctrl+S", or the file's folder.
 * @key: (nullable): what typing is matched against; NULL means @title. */
PalangPaletteItem *palang_palette_item_new          (const char *id,
                                                     const char *title,
                                                     const char *subtitle,
                                                     const char *key);

/* Returns: (transfer none) */
const char        *palang_palette_item_get_id       (PalangPaletteItem *self);
const char        *palang_palette_item_get_title    (PalangPaletteItem *self);
/* Returns: (transfer none) (nullable) */
const char        *palang_palette_item_get_subtitle (PalangPaletteItem *self);
/* Returns: (transfer none) */
const char        *palang_palette_item_get_key      (PalangPaletteItem *self);

G_END_DECLS
