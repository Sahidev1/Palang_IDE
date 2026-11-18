/* palang-bracket-pairs.h — brackets and quotes that close themselves.
 *
 * A PalangBracketPairs watches the keys typed into one GtkSourceView:
 *   - typing an opener — ( [ { " ' — also adds its closer after the
 *     cursor: "(" gives "(|)", the cursor between the two;
 *   - typing that closer right before such an added closer steps over it
 *     instead of adding a second one;
 *   - Backspace between an empty added pair "(|)" deletes both;
 *   - an opener typed over a selection puts the pair around it.
 * The "enabled" property turns all of this on or off (the editor binds it
 * to the auto-close-brackets setting).
 *
 * It also lays out a new block: Enter right between "{}", "[]" or "()"
 * puts the closer on its own line and the cursor on an indented line in
 * between. That is always on, and leaves Enter to the completion list
 * while that is open.
 *
 * The keys are handled by functions that tests (and other code) can call
 * directly, without real key presses: palang_bracket_pairs_type_char (),
 * palang_bracket_pairs_backspace (), palang_bracket_pairs_newline ().
 */

#pragma once

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

#define PALANG_TYPE_BRACKET_PAIRS (palang_bracket_pairs_get_type ())

G_DECLARE_FINAL_TYPE (PalangBracketPairs, palang_bracket_pairs, PALANG, BRACKET_PAIRS, GObject)

/* Starts watching the keys typed into @view, which must already be inside
 * its parent widget (the keys are caught there, see the .c file). The
 * view's buffer must not be replaced afterwards.
 * Returns: (transfer full): keep it as long as the view should have
 * self-closing brackets; the view doesn't keep it alive. */
PalangBracketPairs *palang_bracket_pairs_new          (GtkSourceView *view);

gboolean            palang_bracket_pairs_get_enabled  (PalangBracketPairs *self);
void                palang_bracket_pairs_set_enabled  (PalangBracketPairs *self,
                                                       gboolean            enabled);

/* Does what typing @ch at the cursor does with self-closing brackets:
 * adds a pair, steps over an added closer, or wraps the selection.
 * Returns: TRUE if it handled @ch; FALSE if @ch should be typed the normal
 * way (also when disabled). */
gboolean            palang_bracket_pairs_type_char    (PalangBracketPairs *self,
                                                       gunichar            ch);

/* Backspace: deletes both halves of an empty pair this object added, with
 * the cursor between them.
 * Returns: TRUE if it did; FALSE if Backspace should work the normal way. */
gboolean            palang_bracket_pairs_backspace    (PalangBracketPairs *self);

/* Enter: with the cursor right between an opening bracket and its closer
 * ("{|}"), moves the closer to its own line, at the indentation of the
 * opener's line, and puts the cursor on a new line in between, one level
 * deeper (a tab or spaces, as the view's insert-spaces-instead-of-tabs,
 * indent-width and tab-width say). One undo step. Works whether or not
 * "enabled" is set.
 * Returns: TRUE if it did; FALSE if Enter should work the normal way. */
gboolean            palang_bracket_pairs_newline      (PalangBracketPairs *self);

/* ---- Pure helpers (no widgets) ------------------------------------------ */

/* Returns: the closer of the opener @ch (")" for "("; '"' for '"'), or 0
 * if @ch doesn't open a pair. */
gunichar            palang_bracket_pairs_closer_for   (gunichar ch);

/* Whether typing @opener between the characters @before and @after (0 at
 * the start or end of the text) should add its closer: not right before a
 * word character (a letter, digit or "_": "(|foo"), and for a quote also
 * not right after one ("don|t"). */
gboolean            palang_bracket_pairs_should_close (gunichar opener,
                                                       gunichar before,
                                                       gunichar after);

G_END_DECLS
