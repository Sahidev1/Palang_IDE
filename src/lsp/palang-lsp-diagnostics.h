/* palang-lsp-diagnostics.h — a server's errors and warnings in a buffer.
 *
 * Shows the "diagnostics" of one document (from the server's
 * textDocument/publishDiagnostics) in its buffer:
 *
 *   - a wavy underline under each range (tags "palang-lsp-error",
 *     "palang-lsp-warning", "palang-lsp-info")
 *   - an icon in the gutter on the range's first line (GtkSourceMarks with
 *     the same names as categories; a view shows them once set up with
 *     palang_lsp_diagnostics_setup_view ())
 *   - the messages, found by position (for hover popups)
 *
 * Everything moves along with the text as the user edits, until the
 * server sends new diagnostics.
 *
 * A plain struct, not a GObject: it belongs to one PalangLspDocument.
 */

#pragma once

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

typedef struct _PalangLspDiagnostics PalangLspDiagnostics;

/* Returns: (transfer full): diagnostics for @buffer (not referenced: the
 * caller keeps the buffer alive longer than this). Free with
 * palang_lsp_diagnostics_free (). */
PalangLspDiagnostics *palang_lsp_diagnostics_new         (GtkSourceBuffer *buffer);
void                  palang_lsp_diagnostics_free        (PalangLspDiagnostics *self);

/* Replaces what is shown with @diagnostics: the "diagnostics" array of a
 * publishDiagnostics notification (a JSON array of Diagnostic objects). */
void                  palang_lsp_diagnostics_set         (PalangLspDiagnostics *self,
                                                          GVariant             *diagnostics);

/* Removes everything shown. */
void                  palang_lsp_diagnostics_clear       (PalangLspDiagnostics *self);

/* Returns: how many diagnostics are shown. */
guint                 palang_lsp_diagnostics_get_count   (PalangLspDiagnostics *self);

/* Returns: (transfer full) (nullable): the messages of the diagnostics
 * whose range contains @iter (its end included), like "Error: use of
 * undeclared identifier 'y' (clang)"; NULL if there are none. */
char                **palang_lsp_diagnostics_dup_messages_at (PalangLspDiagnostics *self,
                                                              const GtkTextIter    *iter);

/* Makes @view show the gutter icons. */
void                  palang_lsp_diagnostics_setup_view  (GtkSourceView *view);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PalangLspDiagnostics, palang_lsp_diagnostics_free)

G_END_DECLS
