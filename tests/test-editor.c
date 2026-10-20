/* test-editor.c — tests for an editor's Find / Replace and Go to Line.
 *
 * Every test starts with an editor showing TEXT below. Searching is async,
 * so tests wait until the search bar's status (e.g. "2 of 5") shows the
 * expected value.
 */

#include "config.h"

#include "palang-editor.h"

#define TEXT \
  "foo bar\n"         \
  "Foo baz\n"         \
  "foo food foo\n"

/* ---- Helpers ------------------------------------------------------------ */

typedef struct {
  PalangDocument  *document;
  PalangEditor    *editor;
  PalangSearchBar *search_bar;
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  GtkTextIter start;

  fixture->document = palang_document_new ();
  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (fixture->document), TEXT, -1);
  gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (fixture->document), &start);
  gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (fixture->document), &start);

  fixture->editor = g_object_ref_sink (palang_editor_new (fixture->document));
  fixture->search_bar = palang_editor_get_search_bar (fixture->editor);
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_clear_object (&fixture->editor);
  g_clear_object (&fixture->document);
}

static void
wait_for_status (Fixture    *fixture,
                 const char *expected)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  while (g_strcmp0 (palang_search_bar_get_status (fixture->search_bar), expected) != 0)
    {
      if (g_get_monotonic_time () > deadline)
        g_error ("Expected status \"%s\" but got \"%s\"", expected,
                 palang_search_bar_get_status (fixture->search_bar));
      g_main_context_iteration (NULL, FALSE);
    }
}

/* Opens the Find bar and searches for @text from the cursor. */
static void
search (Fixture    *fixture,
        const char *text)
{
  palang_editor_show_search (fixture->editor, FALSE);
  gtk_source_search_settings_set_search_text (palang_search_bar_get_settings (fixture->search_bar),
                                              text);
  palang_search_bar_find (fixture->search_bar, TRUE);
}

static char *
get_text (Fixture *fixture)
{
  GtkTextIter start, end;

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (fixture->document), &start, &end);
  return gtk_text_buffer_get_text (GTK_TEXT_BUFFER (fixture->document), &start, &end, TRUE);
}

static char *
get_selected_text (Fixture *fixture)
{
  GtkTextIter start, end;

  gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (fixture->document), &start, &end);
  return gtk_text_buffer_get_text (GTK_TEXT_BUFFER (fixture->document), &start, &end, TRUE);
}

/* Returns the cursor position as "line:column", both counted from 1. */
static char *
get_cursor (Fixture *fixture)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);
  GtkTextIter iter;

  gtk_text_buffer_get_iter_at_mark (buffer, &iter, gtk_text_buffer_get_insert (buffer));
  return g_strdup_printf ("%d:%d", gtk_text_iter_get_line (&iter) + 1,
                          gtk_text_iter_get_line_offset (&iter) + 1);
}

/* ---- Find --------------------------------------------------------------- */

/* By default case doesn't matter and "food" contains "foo": 5 matches. */
static void
test_find_counts_and_moves (Fixture *fixture, gconstpointer data)
{
  search (fixture, "foo");
  wait_for_status (fixture, "1 of 5");

  palang_search_bar_find (fixture->search_bar, TRUE);
  wait_for_status (fixture, "2 of 5");
}

static void
test_find_wraps_around (Fixture *fixture, gconstpointer data)
{
  search (fixture, "foo");
  wait_for_status (fixture, "1 of 5");

  palang_search_bar_find (fixture->search_bar, FALSE);
  wait_for_status (fixture, "5 of 5");

  palang_search_bar_find (fixture->search_bar, TRUE);
  wait_for_status (fixture, "1 of 5");
}

static void
test_find_options (Fixture *fixture, gconstpointer data)
{
  GtkSourceSearchSettings *settings = palang_search_bar_get_settings (fixture->search_bar);

  search (fixture, "foo");
  wait_for_status (fixture, "1 of 5");

  gtk_source_search_settings_set_case_sensitive (settings, TRUE);
  wait_for_status (fixture, "1 of 4");

  gtk_source_search_settings_set_at_word_boundaries (settings, TRUE);
  wait_for_status (fixture, "1 of 3");

  gtk_source_search_settings_set_case_sensitive (settings, FALSE);
  gtk_source_search_settings_set_at_word_boundaries (settings, FALSE);
  gtk_source_search_settings_set_regex_enabled (settings, TRUE);
  gtk_source_search_settings_set_search_text (settings, "ba[rz]");
  palang_search_bar_find (fixture->search_bar, TRUE);
  wait_for_status (fixture, "1 of 2");
}

static void
test_find_nothing (Fixture *fixture, gconstpointer data)
{
  search (fixture, "missing");
  wait_for_status (fixture, "No results");
}

/* A selection on one line becomes the search text when the bar opens. */
static void
test_open_uses_selection (Fixture *fixture, gconstpointer data)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);
  GtkTextIter start, end;

  gtk_text_buffer_get_iter_at_line_offset (buffer, &start, 1, 4);
  gtk_text_buffer_get_iter_at_line_offset (buffer, &end, 1, 7);
  gtk_text_buffer_select_range (buffer, &start, &end);

  palang_editor_show_search (fixture->editor, FALSE);

  g_assert_cmpstr (gtk_source_search_settings_get_search_text (palang_search_bar_get_settings (fixture->search_bar)),
                   ==, "baz");
  wait_for_status (fixture, "1 of 1");
}

static void
test_close_clears_status (Fixture *fixture, gconstpointer data)
{
  search (fixture, "foo");
  wait_for_status (fixture, "1 of 5");

  palang_search_bar_close (fixture->search_bar);
  g_assert_false (palang_search_bar_is_open (fixture->search_bar));
  g_assert_cmpstr (palang_search_bar_get_status (fixture->search_bar), ==, "");
}

/* ---- Replace ------------------------------------------------------------ */

static void
test_replace_one (Fixture *fixture, gconstpointer data)
{
  g_autofree char *text = NULL;
  g_autofree char *selected = NULL;

  search (fixture, "ba");
  wait_for_status (fixture, "1 of 2");

  palang_search_bar_set_replace_text (fixture->search_bar, "XY");
  palang_search_bar_replace (fixture->search_bar);

  /* After replacing, the next match is selected. */
  wait_for_status (fixture, "1 of 1");
  text = get_text (fixture);
  selected = get_selected_text (fixture);
  g_assert_cmpstr (text, ==, "foo XYr\nFoo baz\nfoo food foo\n");
  g_assert_cmpstr (selected, ==, "ba");
}

static void
test_replace_all (Fixture *fixture, gconstpointer data)
{
  GtkSourceSearchSettings *settings = palang_search_bar_get_settings (fixture->search_bar);
  g_autofree char *text = NULL;

  search (fixture, "foo");
  wait_for_status (fixture, "1 of 5");
  gtk_source_search_settings_set_at_word_boundaries (settings, TRUE);
  wait_for_status (fixture, "1 of 4");

  palang_search_bar_set_replace_text (fixture->search_bar, "x");
  g_assert_cmpuint (palang_search_bar_replace_all (fixture->search_bar), ==, 4);

  text = get_text (fixture);
  g_assert_cmpstr (text, ==, "x bar\nx baz\nx food x\n");
}

/* ---- Go to line --------------------------------------------------------- */

static void
test_go_to (Fixture *fixture, gconstpointer data)
{
  struct {
    const char *spec;
    const char *cursor;
  } cases[] = {
    { "2",     "2:1" },
    { "2:5",   "2:5" },
    { " 3:2 ", "3:2" },
    { "2:999", "2:8" },  /* past the end of the line → end of line */
    { "99",    "4:1" },  /* past the last line → last line (empty) */
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      g_autofree char *cursor = NULL;

      g_assert_true (palang_editor_go_to (fixture->editor, cases[i].spec));
      cursor = get_cursor (fixture);
      g_assert_cmpstr (cursor, ==, cases[i].cursor);
    }
}

static void
test_go_to_invalid (Fixture *fixture, gconstpointer data)
{
  const char *invalid[] = { "", "abc", "0", "2:0", "2:x", "-1", "1:2:3" };

  for (guint i = 0; i < G_N_ELEMENTS (invalid); i++)
    g_assert_false (palang_editor_go_to (fixture->editor, invalid[i]));
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  adw_init ();
  gtk_source_init ();

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD_TEST ("/editor/find-counts-and-moves", test_find_counts_and_moves);
  ADD_TEST ("/editor/find-wraps-around", test_find_wraps_around);
  ADD_TEST ("/editor/find-options", test_find_options);
  ADD_TEST ("/editor/find-nothing", test_find_nothing);
  ADD_TEST ("/editor/open-uses-selection", test_open_uses_selection);
  ADD_TEST ("/editor/close-clears-status", test_close_clears_status);
  ADD_TEST ("/editor/replace-one", test_replace_one);
  ADD_TEST ("/editor/replace-all", test_replace_all);
  ADD_TEST ("/editor/go-to", test_go_to);
  ADD_TEST ("/editor/go-to-invalid", test_go_to_invalid);

#undef ADD_TEST

  return g_test_run ();
}
