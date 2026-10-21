/* palang-lsp-hover-provider.h — popups when the pointer rests on text.
 *
 * A GtkSourceHoverProvider: GtkSourceView asks it what to show when the
 * mouse pointer rests over the text for a moment. It shows the messages
 * of the diagnostics under the pointer. (Phase 2c adds the server's own
 * hover information here.)
 *
 * Added to each view by palang_lsp_manager_add_view ().
 */

#pragma once

#include <gtksourceview/gtksource.h>

#include "palang-lsp-manager.h"

G_BEGIN_DECLS

#define PALANG_TYPE_LSP_HOVER_PROVIDER (palang_lsp_hover_provider_get_type ())
G_DECLARE_FINAL_TYPE (PalangLspHoverProvider, palang_lsp_hover_provider,
                      PALANG, LSP_HOVER_PROVIDER, GObject)

/* Returns: (transfer full): a provider that finds documents through
 * @manager (not referenced: once the manager is gone, it shows nothing). */
PalangLspHoverProvider *palang_lsp_hover_provider_new (PalangLspManager *manager);

G_END_DECLS
