/* test-fuzzy.c — fuzzy matching for the palette. No display needed. */

#include "config.h"

#include "palang-fuzzy.h"

static int
score_of (const char *pattern,
          const char *text)
{
  int score = G_MININT;

  g_assert_true (palang_fuzzy_match (pattern, text, &score));
  return score;
}

static void
test_matches (void)
{
  /* Letters in order, ignoring case, gaps allowed. */
  g_assert_true (palang_fuzzy_match ("sva", "Save All", NULL));
  g_assert_true (palang_fuzzy_match ("SAVE", "save", NULL));
  g_assert_true (palang_fuzzy_match ("appwin", "src/app/palang-window.c", NULL));
  g_assert_true (palang_fuzzy_match ("ö", "Schön", NULL));
  g_assert_true (palang_fuzzy_match ("", "anything", NULL));

  /* Wrong order or missing letters. */
  g_assert_false (palang_fuzzy_match ("vs", "Save", NULL));
  g_assert_false (palang_fuzzy_match ("savex", "Save All", NULL));
  g_assert_false (palang_fuzzy_match ("a", "", NULL));
}

static void
test_ranking (void)
{
  /* Word starts beat letters in the middle of words. */
  g_assert_cmpint (score_of ("sa", "Save All"), >, score_of ("sa", "Usage"));
  g_assert_cmpint (score_of ("tp", "Toggle Panel"), >, score_of ("tp", "Output"));

  /* Consecutive letters beat scattered ones. */
  g_assert_cmpint (score_of ("pan", "palang-panel.c"), >, score_of ("pan", "palang-application.c"));

  /* camelCase counts as words. */
  g_assert_cmpint (score_of ("pp", "PalangPanel"), >, score_of ("pp", "Popup"));

  /* Path parts count as words: "aw" for app/window. */
  g_assert_cmpint (score_of ("aw", "src/app/window.c"), >, score_of ("aw", "src/draw.c"));
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/fuzzy/matches", test_matches);
  g_test_add_func ("/fuzzy/ranking", test_ranking);

  return g_test_run ();
}
