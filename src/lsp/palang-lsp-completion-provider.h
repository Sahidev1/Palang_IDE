/* palang-lsp-completion-provider.h — completion from language servers.
 *
 * A GtkSourceCompletionProvider: GtkSourceView asks it for proposals
 * while the user types a word, after one of the server's trigger
 * characters (like "." or "->"), and on Ctrl+Space. It asks the
 * document's server, filters the answer by what was typed (fuzzy, best
 * match first) and inserts the chosen entry.
 *
 * Added to each view by palang_lsp_manager_add_view ().
 */

#pragma once

#include <gtksourceview/gtksource.h>

#include "palang-lsp-manager.h"

G_BEGIN_DECLS

#define PALANG_TYPE_LSP_COMPLETION_PROVIDER (palang_lsp_completion_provider_get_type ())
G_DECLARE_FINAL_TYPE (PalangLspCompletionProvider, palang_lsp_completion_provider,
                      PALANG, LSP_COMPLETION_PROVIDER, GObject)

/* Returns: (transfer full): a provider for @view (not referenced; add the
 * provider to the view's completion), finding documents through @manager
 * (not referenced: once the manager is gone, it offers nothing). It also
 * watches @view's keys (from its parent, which it must already have):
 * Enter after a word that already is the first entry starts a new line
 * instead of taking the entry. */
PalangLspCompletionProvider *palang_lsp_completion_provider_new (PalangLspManager *manager,
                                                                 GtkSourceView    *view);

G_END_DECLS
