/* palang-lsp-document.h — one open document, as a language server sees it.
 *
 * Keeps a server's copy of a PalangDocument's text in step with the
 * buffer ("document sync" in the protocol):
 *
 *   didOpen    when the server is ready and the document has a file (an
 *              untitled document is sent once it is saved), and again
 *              after the server restarted
 *   didChange  on every edit: the edited range, or the whole text if the
 *              server asked for that
 *   didSave    after saving
 *   didClose   when the document leaves the server (tab closed, other
 *              server, Save As to another name: close + open)
 *
 * Each change increases the document's version. Replies that were asked
 * for an older version are stale and should be dropped.
 *
 * It also shows the server's diagnostics in the document (see
 * palang-lsp-diagnostics.h), and removes them when the document leaves
 * the server. Signal "diagnostics-changed" (no arguments): they were
 * replaced or removed.
 *
 * Created by PalangLspManager, one per tracked document.
 */

#pragma once

#include <glib-object.h>

#include "palang-document.h"
#include "palang-lsp-client.h"

G_BEGIN_DECLS

#define PALANG_TYPE_LSP_DOCUMENT (palang_lsp_document_get_type ())
G_DECLARE_FINAL_TYPE (PalangLspDocument, palang_lsp_document, PALANG, LSP_DOCUMENT, GObject)

/* Returns: (transfer full): an LSP document for @document, not yet on any
 * server. */
PalangLspDocument *palang_lsp_document_new          (PalangDocument    *document);

/* Returns: (transfer none): the document. */
PalangDocument    *palang_lsp_document_get_document (PalangLspDocument *self);

/* Returns: (transfer none) (nullable): the server the document belongs
 * to. */
PalangLspClient   *palang_lsp_document_get_client   (PalangLspDocument *self);

/* Moves the document to @client (nullable: to no server). Closes it on
 * the old server and opens it on the new one as soon as that is ready.
 * Setting the same client again re-opens the document only if its file or
 * language changed. */
void               palang_lsp_document_set_client   (PalangLspDocument *self,
                                                     PalangLspClient   *client);

/* Returns: TRUE if the server has the document open. */
gboolean           palang_lsp_document_is_open      (PalangLspDocument *self);

/* Returns: (transfer none) (nullable): the URI the server knows the
 * document by, NULL if it isn't open. */
const char        *palang_lsp_document_get_uri      (PalangLspDocument *self);

/* Returns: the version the server has of the text (the last one sent). */
gint64             palang_lsp_document_get_version  (PalangLspDocument *self);

/* Shows @diagnostics (the "diagnostics" array of publishDiagnostics),
 * computed by the server for @version (-1 if the server didn't say).
 * Diagnostics for an older version are dropped: their positions belong to
 * text that has changed since, and the server sends new ones anyway. */
void               palang_lsp_document_set_diagnostics (PalangLspDocument *self,
                                                        gint64             version,
                                                        GVariant          *diagnostics);

/* Returns: (transfer full): a PalangLspProblem for each diagnostic shown
 * (a GPtrArray, freed with it; empty for an untitled document). The
 * "diagnostics-changed" signal says when this changes. */
GPtrArray         *palang_lsp_document_dup_problems (PalangLspDocument *self);

/* Returns: how many diagnostics are shown. */
guint              palang_lsp_document_get_n_diagnostics (PalangLspDocument *self);

/* Returns: (transfer full) (nullable): the messages of the diagnostics at
 * @iter, NULL if there are none. */
char             **palang_lsp_document_dup_messages_at (PalangLspDocument *self,
                                                       const GtkTextIter *iter);

G_END_DECLS
