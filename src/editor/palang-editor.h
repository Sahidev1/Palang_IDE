/* palang-editor.h — the widget shown inside one tab.
 *
 * A PalangEditor displays one PalangDocument in a GtkSourceView. It also
 * provides the text for its tab: a title ("main.c", or "• main.c" while
 * there are unsaved changes) and the file's path for the tooltip.
 */

#pragma once

#include <adwaita.h>

#include "palang-document.h"
#include "palang-search-bar.h"

G_BEGIN_DECLS

#define PALANG_TYPE_EDITOR (palang_editor_get_type ())

G_DECLARE_FINAL_TYPE (PalangEditor, palang_editor, PALANG, EDITOR, AdwBin)

PalangEditor   *palang_editor_new          (PalangDocument *document);

/* Returns: (transfer none) */
PalangDocument *palang_editor_get_document (PalangEditor *self);

/* Returns: (transfer none) */
GtkSourceView  *palang_editor_get_view     (PalangEditor *self);

/* Returns: (transfer none) */
PalangSearchBar *palang_editor_get_search_bar (PalangEditor *self);

/* Opens the Find bar (or Find and Replace when @replace is TRUE). */
void           palang_editor_show_search   (PalangEditor *self,
                                            gboolean      replace);

/* Selects the next/previous match; opens the Find bar if it's closed. */
void           palang_editor_find_next     (PalangEditor *self,
                                            gboolean      forward);

/* Opens the "Go to line" bar. */
void           palang_editor_show_go_to    (PalangEditor *self);

/* Moves the cursor to @spec: "42" (line 42) or "42:10" (line 42, column
 * 10), both counted from 1, and scrolls it into view. Out-of-range values
 * go to the nearest valid position. Returns: FALSE if @spec isn't valid. */
gboolean       palang_editor_go_to         (PalangEditor *self,
                                            const char   *spec);

/* Returns: (transfer full): tab title, with a "• " prefix when modified. */
char           *palang_editor_dup_title    (PalangEditor *self);

/* Returns: (transfer full) (nullable): the file's path with the home folder
 * shown as "~", or NULL for a never-saved document. */
char           *palang_editor_dup_path     (PalangEditor *self);

G_END_DECLS
