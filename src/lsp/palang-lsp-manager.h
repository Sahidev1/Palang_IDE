/* palang-lsp-manager.h — the language servers of one window.
 *
 * The window tells the manager which documents are open and which folder
 * is the project. The manager starts one language server (PalangLspClient)
 * per server needed by those documents: opening the first .py file starts
 * pylsp, the first .c or .h file clangd. A server serves the whole folder.
 * Each document is kept in sync with its server (PalangLspDocument), and
 * the server's diagnostics are shown in it; views set up with
 * palang_lsp_manager_add_view () show them in the gutter and on hover.
 *
 * Servers are started again when the folder changes, when the command of
 * a server changes in the settings (e.g. `palang set python-server …`),
 * and on palang_lsp_manager_restart ().
 *
 * Signal:
 *   "server-failed"  (const char *message): a server isn't installed or
 *                    keeps crashing; @message is ready to show to the user.
 *                    Emitted once per server until the next restart.
 */

#pragma once

#include <glib-object.h>

#include "palang-document.h"
#include "palang-lsp-client.h"
#include "palang-lsp-document.h"
#include "palang-lsp-problem.h"
#include "palang-settings.h"

G_BEGIN_DECLS

#define PALANG_TYPE_LSP_MANAGER (palang_lsp_manager_get_type ())
G_DECLARE_FINAL_TYPE (PalangLspManager, palang_lsp_manager, PALANG, LSP_MANAGER, GObject)

/* Returns: (transfer full): a manager that reads the servers' commands
 * from @settings. Its "folder" property (GFile, nullable) is the project
 * folder; bind it to the file tree's root. */
PalangLspManager *palang_lsp_manager_new             (PalangSettings   *settings);

GFile            *palang_lsp_manager_get_folder      (PalangLspManager *self);
void              palang_lsp_manager_set_folder      (PalangLspManager *self,
                                                      GFile            *folder);

/* An editor tab opened or closed. Adding a document starts its server if
 * needed (also later, when the document gets a language, e.g. on Save As
 * with a .py name). */
void              palang_lsp_manager_add_document    (PalangLspManager *self,
                                                      PalangDocument   *document);
void              palang_lsp_manager_remove_document (PalangLspManager *self,
                                                      PalangDocument   *document);

/* Sets up @view (showing one of the added documents) for language
 * features: icons in the gutter, popups when the pointer rests on text,
 * completion while typing. */
void              palang_lsp_manager_add_view        (PalangLspManager *self,
                                                      GtkSourceView    *view);

/* Asks @document's server where the name at @iter is defined
 * ("textDocument/definition"). Fails with G_IO_ERROR_NOT_CONNECTED if
 * the document has no server or it isn't ready, and with
 * G_IO_ERROR_CANCELLED if the text changed before the answer came (the
 * answer would be about old text). */
void              palang_lsp_manager_find_definition_async  (PalangLspManager    *self,
                                                             PalangDocument      *document,
                                                             const GtkTextIter   *iter,
                                                             GCancellable        *cancellable,
                                                             GAsyncReadyCallback  callback,
                                                             gpointer             user_data);

/* @file: (out) (transfer full) (nullable): where the definition is, NULL
 * if the server found none. @line, @column: (out): its position, counted
 * from 0, the column in characters. (If @file isn't open, the column is
 * the server's UTF-16 count, which differs only after characters like
 * emoji on that line.)
 * Returns: FALSE on error. */
gboolean          palang_lsp_manager_find_definition_finish (PalangLspManager  *self,
                                                             GAsyncResult      *result,
                                                             GFile            **file,
                                                             guint             *line,
                                                             guint             *column,
                                                             GError           **error);

/* Asks @document's server about the name at @iter
 * ("textDocument/hover"): its type, signature, documentation. Fails like
 * find_definition_async (), and with G_IO_ERROR_NOT_SUPPORTED if the
 * server doesn't offer hover information. */
void              palang_lsp_manager_hover_async     (PalangLspManager    *self,
                                                      PalangDocument      *document,
                                                      const GtkTextIter   *iter,
                                                      GCancellable        *cancellable,
                                                      GAsyncReadyCallback  callback,
                                                      gpointer             user_data);

/* Returns: (transfer full) (nullable): the answer as Markdown (turn it
 * into markup with palang_lsp_markdown_to_markup ()), NULL if the server
 * has nothing to say there or on error (check @error). */
char             *palang_lsp_manager_hover_finish    (PalangLspManager  *self,
                                                      GAsyncResult      *result,
                                                      GError           **error);

/* Asks @document's server what could be typed at @iter
 * ("textDocument/completion"). @trigger_character: the character just
 * typed if it is one of the server's trigger characters (like "."), else
 * 0. Fails like hover_async (). Unlike the other requests, an answer that
 * arrives after more typing is kept: the caller filters it by what was
 * typed meanwhile. */
void              palang_lsp_manager_complete_async  (PalangLspManager    *self,
                                                      PalangDocument      *document,
                                                      const GtkTextIter   *iter,
                                                      gunichar             trigger_character,
                                                      GCancellable        *cancellable,
                                                      GAsyncReadyCallback  callback,
                                                      gpointer             user_data);

/* @is_incomplete: (out) (optional): TRUE if the server would find more
 * as more is typed (ask again then).
 * Returns: (transfer full) (nullable): the PalangLspCompletionItems, in
 * the server's order; NULL on error. */
GListModel       *palang_lsp_manager_complete_finish (PalangLspManager  *self,
                                                      GAsyncResult      *result,
                                                      gboolean          *is_incomplete,
                                                      GError           **error);

/* Asks @document's server for the rest of a completion entry
 * ("completionItem/resolve"): @item is the entry exactly as the server
 * sent it (palang_lsp_completion_item_get_raw ()). Fails with
 * G_IO_ERROR_NOT_SUPPORTED if the server sends everything up front. */
void              palang_lsp_manager_resolve_completion_async  (PalangLspManager    *self,
                                                                PalangDocument      *document,
                                                                GVariant            *item,
                                                                GCancellable        *cancellable,
                                                                GAsyncReadyCallback  callback,
                                                                gpointer             user_data);

/* Returns: (transfer full) (nullable): the full CompletionItem; give it
 * to palang_lsp_completion_item_apply_resolved (). NULL on error. */
GVariant         *palang_lsp_manager_resolve_completion_finish (PalangLspManager  *self,
                                                                GAsyncResult      *result,
                                                                GError           **error);

/* Returns: (transfer none): the errors and warnings of all added
 * documents, as a GListModel of PalangLspProblem, sorted by file and
 * position. It stays up to date as servers send new diagnostics and
 * documents come and go. */
GListModel       *palang_lsp_manager_get_problems    (PalangLspManager *self);

/* Stops every server and starts the ones the open documents need. Also
 * tries again servers that had failed. */
void              palang_lsp_manager_restart         (PalangLspManager *self);

/* Returns: (transfer none) (nullable): the server for @document's
 * language, NULL if there is none (no server for that language, or its
 * setting is empty). It may still be starting, or have failed: see
 * palang_lsp_client_is_ready (). */
PalangLspClient  *palang_lsp_manager_get_client      (PalangLspManager *self,
                                                      PalangDocument   *document);

/* Returns: (transfer none) (nullable): how the server sees @document
 * (open or not, its version), NULL if @document wasn't added. */
PalangLspDocument *palang_lsp_manager_get_lsp_document (PalangLspManager *self,
                                                       PalangDocument   *document);

G_END_DECLS
