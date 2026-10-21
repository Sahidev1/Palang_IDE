/* palang-search-panel.h — the "Search" page of the bottom panel.
 *
 * A search box with Match Case / Whole Word / Regex toggles, and the
 * results grouped by file. Typing searches the folder set with
 * palang_search_panel_set_folder () (it waits for a short pause in the
 * typing first). Clicking a result emits "match-activated"; opening the
 * file is up to whoever listens (the window).
 */

#pragma once

#include <adwaita.h>

#include "palang-search.h"

G_BEGIN_DECLS

#define PALANG_TYPE_SEARCH_PANEL (palang_search_panel_get_type ())

G_DECLARE_FINAL_TYPE (PalangSearchPanel, palang_search_panel, PALANG, SEARCH_PANEL, AdwBin)

PalangSearchPanel *palang_search_panel_new        (void);

/* @folder: (nullable): the folder to search; NULL for none. Also the
 * "folder" property. A search in progress starts again in the new folder. */
void               palang_search_panel_set_folder (PalangSearchPanel *self,
                                                   GFile             *folder);

/* Returns: (transfer none) (nullable) */
GFile             *palang_search_panel_get_folder (PalangSearchPanel *self);

/* Focuses the search box, selecting its text. @text: (nullable): if given,
 * replaces the text first and searches for it right away. */
void               palang_search_panel_focus      (PalangSearchPanel *self,
                                                   const char        *text);

/* Returns: (transfer none): the search engine, e.g. to look at results. */
PalangSearch      *palang_search_panel_get_search (PalangSearchPanel *self);

/* Signal "match-activated" (PalangSearchMatch *match): a result was
 * clicked (or chosen with Enter). */

G_END_DECLS
