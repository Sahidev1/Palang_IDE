/* palang-directory-model.h — the live contents of one folder, as a list.
 *
 * A GListModel of GFileInfo, one per entry in the folder. It reads the
 * folder in the background and then watches it, adding and removing entries
 * as files are created, deleted, renamed, or moved in and out.
 *
 * We use this instead of GTK's GtkDirectoryList, which (in GTK 4.14) misses
 * files that appear through a rename — e.g. editors that save by writing a
 * temporary file and renaming it, or `mv a.c b.c`.
 *
 * Each GFileInfo has the entry's GFile in its "standard::file" attribute,
 * the same convention GtkDirectoryList uses.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define PALANG_TYPE_DIRECTORY_MODEL (palang_directory_model_get_type ())

G_DECLARE_FINAL_TYPE (PalangDirectoryModel, palang_directory_model,
                      PALANG, DIRECTORY_MODEL, GObject)

/* @attributes: file attributes to read, e.g. "standard::name,standard::type". */
PalangDirectoryModel *palang_directory_model_new (GFile      *directory,
                                                  const char *attributes);

G_END_DECLS
