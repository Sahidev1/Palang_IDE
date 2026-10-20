/* palang-document.h — one open file: its text, location and on-disk format.
 *
 * PalangDocument *is* a GtkSourceBuffer (it subclasses it), so it can be
 * shown directly in a GtkSourceView. On top of the buffer it adds:
 *   - where the file lives (its GFile, NULL for a new unsaved document),
 *   - loading and saving, keeping the file's encoding and line endings,
 *   - picking the syntax-highlighting language from the file name/content.
 *
 * All file I/O in Palang goes through this type (see PLAN.md §3).
 */

#pragma once

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

#define PALANG_TYPE_DOCUMENT (palang_document_get_type ())

G_DECLARE_FINAL_TYPE (PalangDocument, palang_document,
                      PALANG, DOCUMENT, GtkSourceBuffer)

PalangDocument *palang_document_new           (void);

/* Returns: (transfer none) (nullable): the file's location, or NULL if the
 * document has never been saved. */
GFile          *palang_document_get_file      (PalangDocument *self);

/* Returns: (transfer full): a name to show the user, e.g. "main.c", or
 * "Untitled" for a new document. Free with g_free(). */
char           *palang_document_dup_title     (PalangDocument *self);

/* Loads @file into the document, replacing its text. The document's
 * location only changes if loading succeeds. */
void            palang_document_load_async    (PalangDocument      *self,
                                               GFile               *file,
                                               GCancellable        *cancellable,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data);
gboolean        palang_document_load_finish   (PalangDocument  *self,
                                               GAsyncResult    *result,
                                               GError         **error);

/* Saves the document.
 *
 * @target: where to save ("Save As"), or NULL to save to the current file.
 * @force: if FALSE, saving fails with
 *   GTK_SOURCE_FILE_SAVER_ERROR_EXTERNALLY_MODIFIED when another program
 *   changed the file since we loaded it; pass TRUE to overwrite anyway.
 *
 * On success the document's location becomes @target (if given) and it is
 * marked unmodified. */
void            palang_document_save_async    (PalangDocument      *self,
                                               GFile               *target,
                                               gboolean             force,
                                               GCancellable        *cancellable,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data);
gboolean        palang_document_save_finish   (PalangDocument  *self,
                                               GAsyncResult    *result,
                                               GError         **error);

G_END_DECLS
