/* palang-fuzzy.h — fuzzy matching for the command palette and Quick Open.
 *
 * "sva" matches "Save All": the letters of the pattern appear in the text
 * in order, not necessarily next to each other, ignoring case. A score
 * says how good a match is, so the best ones can be listed first.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Returns: TRUE if every character of @pattern appears in @text in order
 * (ignoring case). Then @score (if not NULL) is set: higher is better.
 * Matches at the start of words ("Save All", "palang-panel.c",
 * "src/app") and runs of consecutive letters score higher; gaps between
 * matched letters lower the score. An empty @pattern matches everything
 * with score 0. */
gboolean palang_fuzzy_match (const char *pattern,
                             const char *text,
                             int        *score);

G_END_DECLS
