/* test-editor.c — tests for an editor's Find / Replace, Go to Line and
 * self-closing brackets.
 *
 * Every test starts with an editor showing TEXT below. Searching is async,
 * so tests wait until the search bar's status (e.g. "2 of 5") shows the
 * expected value. Bracket tests set their own text, with "|" for the
 * cursor, and call the functions the keys call (palang-bracket-pairs.h).
 */

#include "config.h"

#include <string.h>

#include "palang-editor.h"
#include "palang-settings.h"

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

/* ---- Self-closing brackets ---------------------------------------------- */

/* Replaces the text with @text, where "|" marks the cursor, as if the file
 * had just been opened (nothing to undo). */
static void
set_text_with_cursor (Fixture    *fixture,
                      const char *text)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);
  const char *bar = strchr (text, '|');
  g_autofree char *before = NULL;
  g_autofree char *plain = NULL;
  GtkTextIter cursor;

  g_assert_nonnull (bar);
  before = g_strndup (text, bar - text);
  plain = g_strconcat (before, bar + 1, NULL);

  gtk_text_buffer_begin_irreversible_action (buffer);
  gtk_text_buffer_set_text (buffer, plain, -1);
  gtk_text_buffer_end_irreversible_action (buffer);

  gtk_text_buffer_get_iter_at_offset (buffer, &cursor, g_utf8_strlen (before, -1));
  gtk_text_buffer_place_cursor (buffer, &cursor);
}

/* Returns: the text with "|" at the cursor. */
static char *
get_text_with_cursor (Fixture *fixture)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);
  GtkTextIter start, cursor, end;
  g_autofree char *before = NULL;
  g_autofree char *after = NULL;

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  gtk_text_buffer_get_iter_at_mark (buffer, &cursor, gtk_text_buffer_get_insert (buffer));
  before = gtk_text_buffer_get_text (buffer, &start, &cursor, TRUE);
  after = gtk_text_buffer_get_text (buffer, &cursor, &end, TRUE);
  return g_strconcat (before, "|", after, NULL);
}

static void
assert_text (Fixture    *fixture,
             const char *expected)
{
  g_autofree char *text = get_text_with_cursor (fixture);

  g_assert_cmpstr (text, ==, expected);
}

/* Types @text one character at a time, as the keys would: through the
 * bracket pairs first, else inserted the normal way. */
static void
type (Fixture    *fixture,
      const char *text)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);
  PalangBracketPairs *pairs = palang_editor_get_bracket_pairs (fixture->editor);

  for (const char *p = text; *p != '\0'; p = g_utf8_next_char (p))
    {
      gunichar ch = g_utf8_get_char (p);
      char utf8[8] = { 0 };

      if (palang_bracket_pairs_type_char (pairs, ch))
        continue;

      g_unichar_to_utf8 (ch, utf8);
      gtk_text_buffer_begin_user_action (buffer);
      gtk_text_buffer_insert_interactive_at_cursor (buffer, utf8, -1, TRUE);
      gtk_text_buffer_end_user_action (buffer);
    }
}

static void
test_should_close (void)
{
  struct {
    gunichar opener, before, after;
    gboolean expected;
  } cases[] = {
    { '(',  0,   0,    TRUE },
    { '(',  'f', ' ',  TRUE },   /* "f(" a call */
    { '(',  ' ', 'f',  FALSE },  /* before a word */
    { '[',  ' ', '_',  FALSE },
    { '{',  ' ', '7',  FALSE },
    { '{',  ' ', '\n', TRUE },
    { '(',  ' ', ')',  TRUE },
    { '"',  ' ', 0,    TRUE },
    { '"',  'x', 0,    FALSE },  /* right after a word */
    { '\'', 'n', 't',  FALSE },  /* don't */
    { '\'', 'n', ' ',  FALSE },
    { '\'', '(', ')',  TRUE },
    { 'a',  ' ', ' ',  FALSE },  /* not an opener */
    { ')',  ' ', ' ',  FALSE },
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    g_assert_cmpint (palang_bracket_pairs_should_close (cases[i].opener, cases[i].before, cases[i].after),
                     ==, cases[i].expected);

  g_assert_cmpuint (palang_bracket_pairs_closer_for ('{'), ==, '}');
  g_assert_cmpuint (palang_bracket_pairs_closer_for ('\''), ==, '\'');
  g_assert_cmpuint (palang_bracket_pairs_closer_for ('<'), ==, 0);
}

static void
test_brackets_pair (Fixture *fixture, gconstpointer data)
{
  const char *openers[] = { "(", "[", "{", "\"", "'" };
  const char *expected[] = { "x = (|)", "x = [|]", "x = {|}", "x = \"|\"", "x = '|'" };

  for (guint i = 0; i < G_N_ELEMENTS (openers); i++)
    {
      set_text_with_cursor (fixture, "x = |");
      type (fixture, openers[i]);
      assert_text (fixture, expected[i]);
    }

  /* Not before a word, and a quote not after one. */
  set_text_with_cursor (fixture, "|foo");
  type (fixture, "(");
  assert_text (fixture, "(|foo");
  set_text_with_cursor (fixture, "don|");
  type (fixture, "'");
  assert_text (fixture, "don'|");
}

static void
test_brackets_step_over (Fixture *fixture, gconstpointer data)
{
  set_text_with_cursor (fixture, "|");
  type (fixture, "f(a, \"b\")");
  assert_text (fixture, "f(a, \"b\")|");

  set_text_with_cursor (fixture, "|");
  type (fixture, "((");
  assert_text (fixture, "((|))");
  type (fixture, "))");
  assert_text (fixture, "(())|");

  /* A closer the user typed is never stepped over. */
  set_text_with_cursor (fixture, "(|)");
  type (fixture, ")");
  assert_text (fixture, "()|)");
}

/* Once the cursor has left the pair, typing its closer adds another. */
static void
test_brackets_forgotten_after_leaving (Fixture *fixture, gconstpointer data)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);
  GtkTextIter iter;

  set_text_with_cursor (fixture, "x |");
  type (fixture, "(");
  assert_text (fixture, "x (|)");

  gtk_text_buffer_get_start_iter (buffer, &iter);
  gtk_text_buffer_place_cursor (buffer, &iter);
  gtk_text_buffer_get_iter_at_offset (buffer, &iter, 3);
  gtk_text_buffer_place_cursor (buffer, &iter);
  type (fixture, ")");
  assert_text (fixture, "x ()|)");
}

static void
test_brackets_backspace (Fixture *fixture, gconstpointer data)
{
  PalangBracketPairs *pairs = palang_editor_get_bracket_pairs (fixture->editor);

  set_text_with_cursor (fixture, "x|");
  type (fixture, "[");
  assert_text (fixture, "x[|]");
  g_assert_true (palang_bracket_pairs_backspace (pairs));
  assert_text (fixture, "x|");

  /* Not when something is inside, nor in a pair typed by hand. */
  type (fixture, "[a");
  g_assert_false (palang_bracket_pairs_backspace (pairs));
  set_text_with_cursor (fixture, "[|]");
  g_assert_false (palang_bracket_pairs_backspace (pairs));
}

static void
test_brackets_wrap_selection (Fixture *fixture, gconstpointer data)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);
  GtkTextIter start, end;
  g_autofree char *selected = NULL;

  set_text_with_cursor (fixture, "|foo bar baz");
  gtk_text_buffer_get_iter_at_offset (buffer, &start, 4);
  gtk_text_buffer_get_iter_at_offset (buffer, &end, 7);
  gtk_text_buffer_select_range (buffer, &end, &start);

  type (fixture, "{");
  selected = get_selected_text (fixture);
  g_assert_cmpstr (selected, ==, "bar");
  assert_text (fixture, "foo {bar|} baz");

  /* One undo step takes both halves away. */
  gtk_text_buffer_undo (buffer);
  {
    g_autofree char *text = get_text (fixture);
    g_assert_cmpstr (text, ==, "foo bar baz");
  }
}

static void
test_brackets_one_undo_step (Fixture *fixture, gconstpointer data)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);
  g_autofree char *redone = NULL;

  set_text_with_cursor (fixture, "x = |;");
  type (fixture, "(");
  assert_text (fixture, "x = (|);");

  gtk_text_buffer_undo (buffer);
  assert_text (fixture, "x = |;");
  gtk_text_buffer_redo (buffer);
  redone = get_text (fixture);
  g_assert_cmpstr (redone, ==, "x = ();");
}

/* Turned off in the settings: brackets are typed like any character. */
static void
test_brackets_setting_off (Fixture *fixture, gconstpointer data)
{
  PalangSettings *settings = palang_settings_get_default ();
  PalangBracketPairs *pairs = palang_editor_get_bracket_pairs (fixture->editor);

  g_assert_true (palang_bracket_pairs_get_enabled (pairs));

  g_object_set (settings, "auto-close-brackets", FALSE, NULL);
  g_assert_false (palang_bracket_pairs_get_enabled (pairs));

  set_text_with_cursor (fixture, "x|");
  type (fixture, "(\"");
  assert_text (fixture, "x(\"|");

  g_object_set (settings, "auto-close-brackets", TRUE, NULL);
  g_assert_true (palang_bracket_pairs_get_enabled (pairs));
}

/* ---- Enter between brackets --------------------------------------------- */

static void
test_enter_between_brackets (Fixture *fixture, gconstpointer data)
{
  GtkSourceView *view = palang_editor_get_view (fixture->editor);
  PalangBracketPairs *pairs = palang_editor_get_bracket_pairs (fixture->editor);
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (fixture->document);

  /* Set on the view directly: the test doesn't depend on the settings. */
  g_object_set (view, "tab-width", 4, "indent-width", -1, "insert-spaces-instead-of-tabs", TRUE, NULL);

  set_text_with_cursor (fixture, "  if (x) {|}");
  g_assert_true (palang_bracket_pairs_newline (pairs));
  assert_text (fixture, "  if (x) {\n      |\n  }");

  /* One undo step. */
  gtk_text_buffer_undo (buffer);
  {
    g_autofree char *text = get_text (fixture);
    g_assert_cmpstr (text, ==, "  if (x) {}");
  }

  /* Tabs, and the other brackets. */
  g_object_set (view, "insert-spaces-instead-of-tabs", FALSE, NULL);
  set_text_with_cursor (fixture, "\tf(|)");
  g_assert_true (palang_bracket_pairs_newline (pairs));
  assert_text (fixture, "\tf(\n\t\t|\n\t)");

  g_object_set (view, "insert-spaces-instead-of-tabs", TRUE, "indent-width", 2, NULL);
  set_text_with_cursor (fixture, "a = [|]");
  g_assert_true (palang_bracket_pairs_newline (pairs));
  assert_text (fixture, "a = [\n  |\n]");
}

/* Anywhere else, Enter is left to the view. */
static void
test_enter_elsewhere (Fixture *fixture, gconstpointer data)
{
  PalangBracketPairs *pairs = palang_editor_get_bracket_pairs (fixture->editor);
  const char *texts[] = { "{x|}", "{|x}", "{|]", "\"|\"", "|{}", "{}|", "|" };

  for (guint i = 0; i < G_N_ELEMENTS (texts); i++)
    {
      set_text_with_cursor (fixture, texts[i]);
      g_assert_false (palang_bracket_pairs_newline (pairs));
      assert_text (fixture, texts[i]);
    }
}

/* With auto-close-brackets off, Enter between brackets still works. */
static void
test_enter_with_setting_off (Fixture *fixture, gconstpointer data)
{
  PalangBracketPairs *pairs = palang_editor_get_bracket_pairs (fixture->editor);

  g_object_set (palang_editor_get_view (fixture->editor), "tab-width", 4, "indent-width", -1,
                "insert-spaces-instead-of-tabs", TRUE, NULL);
  palang_bracket_pairs_set_enabled (pairs, FALSE);

  set_text_with_cursor (fixture, "{|}");
  g_assert_true (palang_bracket_pairs_newline (pairs));
  assert_text (fixture, "{\n    |\n}");
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
  g_test_add_func ("/editor/brackets/should-close", test_should_close);
  ADD_TEST ("/editor/brackets/pair", test_brackets_pair);
  ADD_TEST ("/editor/brackets/step-over", test_brackets_step_over);
  ADD_TEST ("/editor/brackets/forgotten-after-leaving", test_brackets_forgotten_after_leaving);
  ADD_TEST ("/editor/brackets/backspace", test_brackets_backspace);
  ADD_TEST ("/editor/brackets/wrap-selection", test_brackets_wrap_selection);
  ADD_TEST ("/editor/brackets/one-undo-step", test_brackets_one_undo_step);
  ADD_TEST ("/editor/brackets/setting-off", test_brackets_setting_off);
  ADD_TEST ("/editor/enter/between-brackets", test_enter_between_brackets);
  ADD_TEST ("/editor/enter/elsewhere", test_enter_elsewhere);
  ADD_TEST ("/editor/enter/with-setting-off", test_enter_with_setting_off);

#undef ADD_TEST

  return g_test_run ();
}
