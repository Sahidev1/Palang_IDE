/* palang-search-match.h — one line found by a project search.
 *
 * A PalangSearchMatch is a line of a file that contains the searched text
 * once or more: the file, the line number, the line's text, and where in
 * the line the matches are. Positions are counted in characters (not
 * bytes), like the editor counts columns.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define PALANG_TYPE_SEARCH_MATCH (palang_search_match_get_type ())

G_DECLARE_FINAL_TYPE (PalangSearchMatch, palang_search_match, PALANG, SEARCH_MATCH, GObject)

/* @path: the file's path for display, relative to the searched folder.
 * @line: line number, counted from 1.
 * @text: the line, without its line break.
 * @ranges: @n_ranges pairs of character offsets into @text, each the
 *   [start, end) of one match, in order. At least one pair. */
PalangSearchMatch *palang_search_match_new        (GFile       *file,
                                                   const char  *path,
                                                   guint        line,
                                                   const char  *text,
                                                   const guint *ranges,
                                                   guint        n_ranges);

/* Returns: (transfer none) */
GFile             *palang_search_match_get_file   (PalangSearchMatch *self);

/* Returns: (transfer none) */
const char        *palang_search_match_get_path   (PalangSearchMatch *self);

guint              palang_search_match_get_line   (PalangSearchMatch *self);

/* The first match's column (counted from 1) and length, in characters. */
guint              palang_search_match_get_column (PalangSearchMatch *self);
guint              palang_search_match_get_length (PalangSearchMatch *self);

/* Returns: (transfer none) */
const char        *palang_search_match_get_text   (PalangSearchMatch *self);

/* Returns: (transfer full): the line as Pango markup for a label, with the
 * matches in bold and leading spaces removed, cut off if very long. */
char              *palang_search_match_dup_markup (PalangSearchMatch *self);

G_END_DECLS
