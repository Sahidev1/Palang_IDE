/* palang-editor.h — the widget shown inside one tab.
 *
 * A PalangEditor displays one PalangDocument in a GtkSourceView. It also
 * provides the text for its tab: a title ("main.c", or "• main.c" while
 * there are unsaved changes) and the file's path for the tooltip.
 */

#pragma once

#include <adwaita.h>

#include "palang-document.h"

G_BEGIN_DECLS

#define PALANG_TYPE_EDITOR (palang_editor_get_type ())

G_DECLARE_FINAL_TYPE (PalangEditor, palang_editor, PALANG, EDITOR, AdwBin)

PalangEditor   *palang_editor_new          (PalangDocument *document);

/* Returns: (transfer none) */
PalangDocument *palang_editor_get_document (PalangEditor *self);

/* Returns: (transfer none) */
GtkSourceView  *palang_editor_get_view     (PalangEditor *self);

/* Returns: (transfer full): tab title, with a "• " prefix when modified. */
char           *palang_editor_dup_title    (PalangEditor *self);

/* Returns: (transfer full) (nullable): the file's path with the home folder
 * shown as "~", or NULL for a never-saved document. */
char           *palang_editor_dup_path     (PalangEditor *self);

G_END_DECLS
