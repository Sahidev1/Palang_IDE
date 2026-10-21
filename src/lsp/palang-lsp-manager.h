/* palang-lsp-manager.h — the language servers of one window.
 *
 * The window tells the manager which documents are open and which folder
 * is the project. The manager starts one language server (PalangLspClient)
 * per server needed by those documents: opening the first .py file starts
 * pylsp, the first .c or .h file clangd. A server serves the whole folder.
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
