/* palang-search-bar.h — Find / Replace bar shown at the top of an editor.
 *
 * Searches the editor's text with GtkSourceView's search engine, which also
 * highlights every match. The search options (text, case, regex, whole
 * words) live in a GtkSourceSearchSettings, which the bar's widgets are
 * kept in sync with.
 */

#pragma once

#include <adwaita.h>
#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

#define PALANG_TYPE_SEARCH_BAR (palang_search_bar_get_type ())

G_DECLARE_FINAL_TYPE (PalangSearchBar, palang_search_bar, PALANG, SEARCH_BAR, AdwBin)

PalangSearchBar        *palang_search_bar_new          (GtkSourceView *view);

/* Shows the bar (with the replace row if @replace) and focuses the search
 * box. A single-line selection becomes the search text. */
void                     palang_search_bar_open         (PalangSearchBar *self,
                                                         gboolean         replace);

/* Hides the bar, removes the highlighting, and focuses the text again. */
void                     palang_search_bar_close        (PalangSearchBar *self);

gboolean                 palang_search_bar_is_open      (PalangSearchBar *self);

/* Selects the next (or previous) match, wrapping around the end. */
void                     palang_search_bar_find         (PalangSearchBar *self,
                                                         gboolean         forward);

/* Replaces the selected match (if the selection is one) and moves to the
 * next match. */
void                     palang_search_bar_replace      (PalangSearchBar *self);

/* Replaces every match. Returns: how many were replaced. */
guint                    palang_search_bar_replace_all  (PalangSearchBar *self);

/* Returns: (transfer none): search text and options. */
GtkSourceSearchSettings *palang_search_bar_get_settings (PalangSearchBar *self);

void                     palang_search_bar_set_replace_text (PalangSearchBar *self,
                                                             const char      *text);

/* Returns: (transfer none): the status text, e.g. "2 of 5" or "No results". */
const char              *palang_search_bar_get_status   (PalangSearchBar *self);

G_END_DECLS
