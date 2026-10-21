/* palang-rg.h — talking to ripgrep (rg): its arguments and its output.
 *
 * Project search runs `rg --json …` and reads one JSON object per line
 * from it. These two functions are the pure parts of that (no process, no
 * display), so they're easy to test; palang-search.c runs the process.
 */

#pragma once

#include "palang-search-match.h"

G_BEGIN_DECLS

typedef enum {
  PALANG_SEARCH_MATCH_CASE = 1 << 0,  /* otherwise case-insensitive */
  PALANG_SEARCH_WHOLE_WORD = 1 << 1,
  PALANG_SEARCH_REGEX      = 1 << 2,  /* otherwise the text is literal */
} PalangSearchFlags;

/* Returns: (transfer full): the command line that searches for @text in
 * the current folder, e.g. { "rg", "--json", "-i", "-F", "-e", "foo", NULL }. */
char              **palang_rg_build_argv (const char        *text,
                                          PalangSearchFlags  flags);

/* Parses one line of `rg --json` output. @folder is the folder rg ran in
 * (its paths are relative to it).
 * Returns: (transfer full) (nullable): the match described by a "match"
 * line; NULL for the other kinds of lines (begin, end, summary), and NULL
 * with @error set if the line isn't what rg writes. */
PalangSearchMatch  *palang_rg_parse_line (const char  *line,
                                          GFile       *folder,
                                          GError     **error);

G_END_DECLS
