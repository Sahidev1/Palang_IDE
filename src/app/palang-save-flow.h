/* palang-save-flow.h — save a document, asking the user where needed.
 *
 * Wraps palang_document_save_async() with the dialogs a user expects:
 *   - "Save As" file chooser for a new document (or when asked for),
 *   - "File changed on disk, overwrite?" if another program changed it.
 *
 * Used by Save, Save As, Save All, and the "unsaved changes" prompts when
 * closing a tab or the window.
 */

#pragma once

#include <gtk/gtk.h>

#include "palang-document.h"

G_BEGIN_DECLS

/* @parent: window the dialogs belong to.
 * @ask_location: TRUE for "Save As"; a new document always asks.
 *
 * Finishes with TRUE when the document was saved. If the user cancelled a
 * dialog, it finishes with FALSE and a G_IO_ERROR_CANCELLED error, which
 * callers usually ignore (the user already knows they cancelled). */
void     palang_save_document_async  (GtkWindow           *parent,
                                      PalangDocument      *document,
                                      gboolean             ask_location,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data);
gboolean palang_save_document_finish (PalangDocument  *document,
                                      GAsyncResult    *result,
                                      GError         **error);

G_END_DECLS
