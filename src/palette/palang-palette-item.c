/* palang-palette-item.c — one choice in the palette (see the header).
 * A value object: four strings set at creation.
 */

#include "config.h"

#include "palang-palette-item.h"

struct _PalangPaletteItem
{
  GObject parent_instance;

  char *id;
  char *title;
  char *subtitle;  /* nullable */
  char *key;
};

G_DEFINE_FINAL_TYPE (PalangPaletteItem, palang_palette_item, G_TYPE_OBJECT)

const char *
palang_palette_item_get_id (PalangPaletteItem *self)
{
  g_return_val_if_fail (PALANG_IS_PALETTE_ITEM (self), NULL);

  return self->id;
}

const char *
palang_palette_item_get_title (PalangPaletteItem *self)
{
  g_return_val_if_fail (PALANG_IS_PALETTE_ITEM (self), NULL);

  return self->title;
}

const char *
palang_palette_item_get_subtitle (PalangPaletteItem *self)
{
  g_return_val_if_fail (PALANG_IS_PALETTE_ITEM (self), NULL);

  return self->subtitle;
}

const char *
palang_palette_item_get_key (PalangPaletteItem *self)
{
  g_return_val_if_fail (PALANG_IS_PALETTE_ITEM (self), NULL);

  return self->key;
}

static void
palang_palette_item_finalize (GObject *object)
{
  PalangPaletteItem *self = PALANG_PALETTE_ITEM (object);

  g_free (self->id);
  g_free (self->title);
  g_free (self->subtitle);
  g_free (self->key);

  G_OBJECT_CLASS (palang_palette_item_parent_class)->finalize (object);
}

static void
palang_palette_item_class_init (PalangPaletteItemClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = palang_palette_item_finalize;
}

static void
palang_palette_item_init (PalangPaletteItem *self)
{
}

PalangPaletteItem *
palang_palette_item_new (const char *id,
                         const char *title,
                         const char *subtitle,
                         const char *key)
{
  PalangPaletteItem *self;

  g_return_val_if_fail (id != NULL && title != NULL, NULL);

  self = g_object_new (PALANG_TYPE_PALETTE_ITEM, NULL);
  self->id = g_strdup (id);
  self->title = g_strdup (title);
  self->subtitle = g_strdup (subtitle);
  self->key = g_strdup (key != NULL ? key : title);

  return self;
}
