/* palang-search.h — searching all files of a folder, with ripgrep.
 *
 * A PalangSearch runs one search at a time: palang_search_start () stops
 * the previous one, clears the results and starts again. Matches appear in
 * the results list (a GListModel of PalangSearchMatch) while rg is still
 * looking, and "finished" says when it's done.
 *
 * It searches the files on disk, so unsaved changes in the editor aren't
 * found. Files ignored by .gitignore, hidden files and binary files are
 * skipped (rg's defaults).
 */

#pragma once

#include "palang-rg.h"

G_BEGIN_DECLS

#define PALANG_TYPE_SEARCH (palang_search_get_type ())

G_DECLARE_FINAL_TYPE (PalangSearch, palang_search, PALANG, SEARCH, GObject)

/* At most this many matching lines are collected; then rg is stopped and
 * "finished" reports truncated = TRUE. */
#define PALANG_SEARCH_MAX_RESULTS 5000

PalangSearch *palang_search_new         (void);

/* Searches @folder for @text. An empty @text just clears the results.
 * Signal "finished" (gboolean truncated, const char *error_message): the
 * search ended; error_message is NULL unless it failed (rg missing, an
 * invalid regular expression, …), and then it says why. */
void          palang_search_start       (PalangSearch      *self,
                                         GFile             *folder,
                                         const char        *text,
                                         PalangSearchFlags  flags);

/* Stops the running search, keeping the results found so far. */
void          palang_search_stop        (PalangSearch *self);

/* Returns: (transfer none): the matches, a list of PalangSearchMatch in the
 * order rg finds them (grouped by file). */
GListModel   *palang_search_get_results (PalangSearch *self);

/* TRUE while a search runs. Also the "running" property. */
gboolean      palang_search_is_running  (PalangSearch *self);

/* Lists the files in @folder and its subfolders (`rg --files`: the files
 * a search looks at, so not those ignored by .gitignore, nor hidden ones),
 * sorted by path. For Quick Open. */
void          palang_search_list_files_async  (GFile               *folder,
                                               GCancellable        *cancellable,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data);

/* Returns: (transfer full): the files' paths relative to the folder, as
 * file names (bytes; use g_filename_display_name () to show them), or NULL
 * with @error set. */
char        **palang_search_list_files_finish (GAsyncResult  *result,
                                               GError       **error);

G_END_DECLS
