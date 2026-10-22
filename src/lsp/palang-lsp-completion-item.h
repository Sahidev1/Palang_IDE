/* palang-lsp-completion-item.h — one entry of a completion list.
 *
 * Made from one LSP CompletionItem as the server sent it. It knows what
 * to show (label, detail, kind), what typing is matched against (filter
 * text), how to sort it, and what to insert.
 *
 * It implements GtkSourceCompletionProposal, the interface that marks an
 * object as something GtkSourceView's completion popup can list.
 * PalangLspCompletionProvider makes and shows these.
 */

#pragma once

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

#define PALANG_TYPE_LSP_COMPLETION_ITEM (palang_lsp_completion_item_get_type ())
G_DECLARE_FINAL_TYPE (PalangLspCompletionItem, palang_lsp_completion_item,
                      PALANG, LSP_COMPLETION_ITEM, GObject)

/* Returns: (transfer full) (nullable): an item for @item, an LSP
 * CompletionItem (a{sv}); NULL if it has no label. */
PalangLspCompletionItem *palang_lsp_completion_item_new         (GVariant *item);

/* Returns: (transfer none): the label as shown (without the leading
 * spaces some servers add for alignment). */
const char *palang_lsp_completion_item_get_label        (PalangLspCompletionItem *self);

/* Returns: (transfer none): what typing is matched against: the
 * server's "filterText", or the label. */
const char *palang_lsp_completion_item_get_filter_text  (PalangLspCompletionItem *self);

/* Returns: (transfer none): what the server wants it sorted by
 * ("sortText"), or the label. */
const char *palang_lsp_completion_item_get_sort_text    (PalangLspCompletionItem *self);

/* Returns: (transfer none) (nullable): more about it, often a type or a
 * signature ("int", "(int a, int b) -> int"). */
const char *palang_lsp_completion_item_get_detail       (PalangLspCompletionItem *self);

/* Returns: (transfer none) (nullable): what it is, as a word
 * ("Function", "Variable", "Keyword"). */
const char *palang_lsp_completion_item_get_kind_name    (PalangLspCompletionItem *self);

/* Returns: (transfer none): the text to insert. */
const char *palang_lsp_completion_item_get_new_text     (PalangLspCompletionItem *self);

/* Matches the item against @casefold_typed (what was typed, run through
 * g_utf8_casefold ()) and remembers the result for get_score ().
 * Returns: TRUE if it matches. An empty @casefold_typed matches all. */
gboolean    palang_lsp_completion_item_match            (PalangLspCompletionItem *self,
                                                         const char              *casefold_typed);

/* Returns: how well the last match () went: lower is better, G_MAXUINT if
 * it didn't match. */
guint       palang_lsp_completion_item_get_score        (PalangLspCompletionItem *self);

/* Returns: <0 if @a comes before @b in the list (like strcmp): the
 * better match (lower score) first; for equal scores (or nothing typed),
 * the server's order (sort text). */
int         palang_lsp_completion_item_compare          (PalangLspCompletionItem *a,
                                                         PalangLspCompletionItem *b);

/* Inserts the item for the word from @word_start to @cursor in @buffer:
 * the word is replaced with get_new_text () (from where the server says,
 * if it did: "textEdit"), and the server's other edits are made too
 * ("additionalTextEdits", e.g. an #include clangd adds). One user action:
 * one Ctrl+Z undoes it all. */
void        palang_lsp_completion_item_insert           (PalangLspCompletionItem *self,
                                                         GtkTextBuffer           *buffer,
                                                         const GtkTextIter       *word_start,
                                                         const GtkTextIter       *cursor);

G_END_DECLS
