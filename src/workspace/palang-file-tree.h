/* palang-file-tree.h — the folder tree in the sidebar.
 *
 * Shows the contents of a root folder as an expandable tree: folders first,
 * then files, sorted by name; hidden files only when show-hidden is TRUE.
 * The tree updates by itself when files are added, renamed or deleted.
 *
 * Clicking a file emits "file-activated"; clicking a folder expands it.
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define PALANG_TYPE_FILE_TREE (palang_file_tree_get_type ())

G_DECLARE_FINAL_TYPE (PalangFileTree, palang_file_tree, PALANG, FILE_TREE, AdwBin)

PalangFileTree *palang_file_tree_new             (void);

/* @root: (nullable): folder to show, or NULL for none. */
void            palang_file_tree_set_root        (PalangFileTree *self,
                                                  GFile          *root);

/* Returns: (transfer none) (nullable) */
GFile          *palang_file_tree_get_root        (PalangFileTree *self);

void            palang_file_tree_set_show_hidden (PalangFileTree *self,
                                                  gboolean        show_hidden);
gboolean        palang_file_tree_get_show_hidden (PalangFileTree *self);

/* Returns: (transfer none) (nullable): the visible rows, a list of
 * GtkTreeListRow whose items are GFileInfo. NULL when there's no root. */
GListModel     *palang_file_tree_get_rows        (PalangFileTree *self);

/* Returns: (transfer none): the file a row shows. */
GFile          *palang_file_tree_row_get_file    (GtkTreeListRow *row);

G_END_DECLS
