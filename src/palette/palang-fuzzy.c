/* palang-fuzzy.c — fuzzy matching (see the header).
 *
 * The method is simple: walk through the text, taking each pattern
 * character at the first place it fits, and add up points as we go. Taking
 * the first place can miss a better match ("pan" would use the "pa" of
 * "palang-panel.c" and not "panel"), so we try this from every place the
 * pattern's first character appears and keep the best score. That is fast
 * enough to score thousands of file names on every key press.
 *
 * Points per matched character:
 *   +10  at the start of a word (first character, or after a space,
 *        "-", "_", ".", "/", or a capital letter after a lower-case one)
 *   +5   right after the previous matched character
 * and -1 for each character skipped in between, so tight matches win.
 * (Equal scores are sorted by length in the palette, so "Save" comes
 * before "Save All" for "save".)
 */

#include "config.h"

#include "palang-fuzzy.h"

#define WORD_START_BONUS  10
#define CONSECUTIVE_BONUS 5

/* TRUE if @current starts a word. @previous is the character before it
 * (0 at the start of the text). */
static gboolean
is_word_start (gunichar previous,
               gunichar current)
{
  if (previous == 0)
    return TRUE;

  if (previous == ' ' || previous == '-' || previous == '_' ||
      previous == '.' || previous == '/')
    return TRUE;

  /* camelCase: "PalangPanel" has words "Palang" and "Panel". */
  return g_unichar_islower (previous) && g_unichar_isupper (current);
}

/* Matches @pattern in @text starting at @start (a position in @text where
 * the pattern's first character is), taking each character at the first
 * place it fits. Returns: FALSE if the pattern doesn't fit; otherwise
 * @score is set. */
static gboolean
match_from (const char *pattern,
            const char *text,
            const char *start,
            int        *score)
{
  const char *p = pattern;
  gunichar previous = start == text ? 0 : g_utf8_get_char (g_utf8_prev_char (start));
  gboolean previous_matched = FALSE;
  int points = 0;

  for (const char *t = start; *t != '\0' && *p != '\0'; t = g_utf8_next_char (t))
    {
      gunichar current = g_utf8_get_char (t);

      if (g_unichar_tolower (current) == g_unichar_tolower (g_utf8_get_char (p)))
        {
          if (is_word_start (previous, current))
            points += WORD_START_BONUS;
          if (previous_matched)
            points += CONSECUTIVE_BONUS;

          previous_matched = TRUE;
          p = g_utf8_next_char (p);
        }
      else
        {
          points -= 1;  /* a gap between matched characters */
          previous_matched = FALSE;
        }

      previous = current;
    }

  /* Characters of the pattern left over: no match. */
  if (*p != '\0')
    return FALSE;

  *score = points;
  return TRUE;
}

gboolean
palang_fuzzy_match (const char *pattern,
                    const char *text,
                    int        *score)
{
  gunichar first;
  gboolean found = FALSE;
  int best = 0;

  g_return_val_if_fail (pattern != NULL && text != NULL, FALSE);

  if (pattern[0] == '\0')
    {
      if (score != NULL)
        *score = 0;
      return TRUE;
    }

  first = g_unichar_tolower (g_utf8_get_char (pattern));

  for (const char *t = text; *t != '\0'; t = g_utf8_next_char (t))
    {
      int points;

      if (g_unichar_tolower (g_utf8_get_char (t)) != first)
        continue;

      /* If the pattern doesn't fit from here, it won't from further on. */
      if (!match_from (pattern, text, t, &points))
        break;

      if (!found || points > best)
        best = points;
      found = TRUE;
    }

  if (found && score != NULL)
    *score = best;

  return found;
}
