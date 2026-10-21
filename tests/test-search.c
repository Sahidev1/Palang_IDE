/* test-search.c — project search: rg's arguments, parsing its JSON output,
 * and real searches with rg in a temporary folder. No display needed.
 */

#include "config.h"

#include <string.h>

#include <glib/gstdio.h>

#include "palang-search.h"

/* ---- Parsing (no rg needed) --------------------------------------------- */

static void
test_build_argv (void)
{
  g_auto (GStrv) literal = palang_rg_build_argv ("-x", 0);
  g_auto (GStrv) regex = palang_rg_build_argv ("a.c", PALANG_SEARCH_MATCH_CASE |
                                                      PALANG_SEARCH_WHOLE_WORD |
                                                      PALANG_SEARCH_REGEX);

  g_assert_cmpstrv (literal, ((const char *[]) { "rg", "--json", "--no-messages",
                                                 "--sort", "path", "--ignore-case", "--fixed-strings",
                                                 "-e", "-x", NULL }));
  g_assert_cmpstrv (regex, ((const char *[]) { "rg", "--json", "--no-messages",
                                               "--sort", "path", "--word-regexp", "-e", "a.c", NULL }));
}

static void
test_parse_match (void)
{
  g_autoptr (GFile) folder = g_file_new_for_path ("/project");
  g_autoptr (GFile) expected_file = g_file_new_for_path ("/project/src/main.c");
  g_autoptr (PalangSearchMatch) match = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *markup = NULL;

  /* Two matches; "ö" is 2 bytes, so byte offsets 7..9 are characters 6..7. */
  match = palang_rg_parse_line ("{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"src/main.c\"},"
                                "\"lines\":{\"text\":\"    ö <x> ö\\n\"},\"line_number\":12,"
                                "\"submatches\":[{\"match\":{\"text\":\"ö\"},\"start\":4,\"end\":6},"
                                "{\"match\":{\"text\":\"ö\"},\"start\":11,\"end\":13}]}}",
                                folder, &error);
  g_assert_no_error (error);
  g_assert_nonnull (match);

  g_assert_true (g_file_equal (palang_search_match_get_file (match), expected_file));
  g_assert_cmpstr (palang_search_match_get_path (match), ==, "src/main.c");
  g_assert_cmpuint (palang_search_match_get_line (match), ==, 12);
  g_assert_cmpuint (palang_search_match_get_column (match), ==, 5);  /* counted from 1 */
  g_assert_cmpuint (palang_search_match_get_length (match), ==, 1);
  g_assert_cmpstr (palang_search_match_get_text (match), ==, "    ö <x> ö");

  /* Indentation dropped, matches bold, "<" escaped. */
  markup = palang_search_match_dup_markup (match);
  g_assert_cmpstr (markup, ==, "<b>ö</b> &lt;x&gt; <b>ö</b>");
}

/* A Latin-1 line and file name arrive base64-encoded. */
static void
test_parse_bytes (void)
{
  g_autoptr (GFile) folder = g_file_new_for_path ("/project");
  g_autoptr (PalangSearchMatch) match = NULL;
  g_autofree char *line = NULL;
  g_autofree char *path64 = g_base64_encode ((const guchar *) "caf\xe9.txt", 8);
  g_autofree char *text64 = g_base64_encode ((const guchar *) "caf\xe9 match\n", 11);

  line = g_strdup_printf ("{\"type\":\"match\",\"data\":{\"path\":{\"bytes\":\"%s\"},"
                          "\"lines\":{\"bytes\":\"%s\"},\"line_number\":1,"
                          "\"submatches\":[{\"match\":{\"text\":\"match\"},\"start\":5,\"end\":10}]}}",
                          path64, text64);
  match = palang_rg_parse_line (line, folder, NULL);

  g_assert_nonnull (match);
  /* The invalid byte becomes one replacement character, so the match
   * still starts at character 5 (column 6). */
  g_assert_cmpuint (palang_search_match_get_column (match), ==, 6);
  g_assert_cmpuint (palang_search_match_get_length (match), ==, 5);
  g_assert_true (g_utf8_validate (palang_search_match_get_text (match), -1, NULL));
  g_assert_true (g_utf8_validate (palang_search_match_get_path (match), -1, NULL));
}

static void
test_parse_other_lines (void)
{
  g_autoptr (GFile) folder = g_file_new_for_path ("/project");
  g_autoptr (GError) error = NULL;

  /* Not a match: NULL, no error. */
  g_assert_null (palang_rg_parse_line ("{\"type\":\"begin\",\"data\":{\"path\":{\"text\":\"a\"}}}",
                                       folder, &error));
  g_assert_no_error (error);

  /* Not JSON / not rg's shape: NULL with an error. */
  g_assert_null (palang_rg_parse_line ("hello", folder, &error));
  g_assert_nonnull (error);
  g_clear_error (&error);
  g_assert_null (palang_rg_parse_line ("{\"type\":\"match\",\"data\":{}}", folder, &error));
  g_assert_nonnull (error);
}

/* ---- Real searches with rg ---------------------------------------------- */

typedef struct {
  char         *dir;
  GFile        *folder;
  PalangSearch *search;
  gboolean      finished;
  gboolean      truncated;
  char         *error_message;
} Fixture;

static void
on_finished (PalangSearch *search,
             gboolean      truncated,
             const char   *error_message,
             Fixture      *fixture)
{
  fixture->finished = TRUE;
  fixture->truncated = truncated;
  g_free (fixture->error_message);
  fixture->error_message = g_strdup (error_message);
}

static void
write_file (Fixture *fixture, const char *name, const char *contents)
{
  g_autofree char *path = g_build_filename (fixture->dir, name, NULL);

  g_assert_true (g_file_set_contents (path, contents, -1, NULL));
}

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", NULL);
  fixture->folder = g_file_new_for_path (fixture->dir);
  fixture->search = palang_search_new ();
  g_signal_connect (fixture->search, "finished", G_CALLBACK (on_finished), fixture);
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GDir) dir = g_dir_open (fixture->dir, 0, NULL);
  const char *name;

  palang_search_stop (fixture->search);
  g_clear_object (&fixture->search);
  g_clear_object (&fixture->folder);
  g_free (fixture->error_message);

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *path = g_build_filename (fixture->dir, name, NULL);
      g_unlink (path);
    }
  g_rmdir (fixture->dir);
  g_free (fixture->dir);
}

/* Starts a search and runs the main loop until it finishes (5 s at most). */
static void
search_and_wait (Fixture           *fixture,
                 const char        *text,
                 PalangSearchFlags  flags)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  fixture->finished = FALSE;
  palang_search_start (fixture->search, fixture->folder, text, flags);

  while (!fixture->finished)
    {
      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out waiting for the search");
      g_main_context_iteration (NULL, FALSE);
    }
}

static guint
n_results (Fixture *fixture)
{
  return g_list_model_get_n_items (palang_search_get_results (fixture->search));
}

static void
test_search_finds_matches (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSearchMatch) match = NULL;
  g_autofree char *expected_path = g_build_filename (fixture->dir, "b.c", NULL);
  g_autofree char *found_path = NULL;

  write_file (fixture, "a.txt", "nothing here\n");
  write_file (fixture, "b.c", "int x;\nint Needle = 1;\n");

  search_and_wait (fixture, "needle", 0);  /* case-insensitive by default */

  g_assert_null (fixture->error_message);
  g_assert_false (fixture->truncated);
  g_assert_false (palang_search_is_running (fixture->search));
  g_assert_cmpuint (n_results (fixture), ==, 1);

  match = g_list_model_get_item (palang_search_get_results (fixture->search), 0);
  found_path = g_file_get_path (palang_search_match_get_file (match));
  g_assert_cmpstr (found_path, ==, expected_path);
  g_assert_cmpuint (palang_search_match_get_line (match), ==, 2);
  g_assert_cmpuint (palang_search_match_get_column (match), ==, 5);

  /* Match Case: "needle" no longer matches "Needle". */
  search_and_wait (fixture, "needle", PALANG_SEARCH_MATCH_CASE);
  g_assert_cmpuint (n_results (fixture), ==, 0);
  g_assert_null (fixture->error_message);
}

static void
test_search_literal_and_regex (Fixture *fixture, gconstpointer data)
{
  write_file (fixture, "a.txt", "a.c\nabc\n");

  search_and_wait (fixture, "a.c", 0);  /* literal: only "a.c" */
  g_assert_cmpuint (n_results (fixture), ==, 1);

  search_and_wait (fixture, "a.c", PALANG_SEARCH_REGEX);  /* "." matches any */
  g_assert_cmpuint (n_results (fixture), ==, 2);

  search_and_wait (fixture, "(", PALANG_SEARCH_REGEX);  /* invalid regex */
  g_assert_cmpuint (n_results (fixture), ==, 0);
  g_assert_nonnull (fixture->error_message);
}

/* Results stop at PALANG_SEARCH_MAX_RESULTS. */
static void
test_search_truncates (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GString) contents = g_string_new (NULL);

  for (guint i = 0; i < PALANG_SEARCH_MAX_RESULTS + 100; i++)
    g_string_append (contents, "hit\n");
  write_file (fixture, "many.txt", contents->str);

  search_and_wait (fixture, "hit", 0);
  g_assert_true (fixture->truncated);
  g_assert_cmpuint (n_results (fixture), ==, PALANG_SEARCH_MAX_RESULTS);
}

/* A new search replaces the running one: no old results mix in. */
static void
test_search_restart (Fixture *fixture, gconstpointer data)
{
  write_file (fixture, "a.txt", "apple\nbanana\n");

  palang_search_start (fixture->search, fixture->folder, "apple", 0);
  search_and_wait (fixture, "banana", 0);

  g_assert_cmpuint (n_results (fixture), ==, 1);

  /* An empty text clears the results without starting rg. */
  palang_search_start (fixture->search, fixture->folder, "", 0);
  g_assert_cmpuint (n_results (fixture), ==, 0);
  g_assert_false (palang_search_is_running (fixture->search));
}

int
main (int   argc,
      char *argv[])
{
  g_autofree char *rg = g_find_program_in_path ("rg");

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/search/build-argv", test_build_argv);
  g_test_add_func ("/search/parse-match", test_parse_match);
  g_test_add_func ("/search/parse-bytes", test_parse_bytes);
  g_test_add_func ("/search/parse-other-lines", test_parse_other_lines);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  /* These need rg; skipped when it isn't installed (a source build). */
  if (rg != NULL)
    {
      ADD_TEST ("/search/finds-matches", test_search_finds_matches);
      ADD_TEST ("/search/literal-and-regex", test_search_literal_and_regex);
      ADD_TEST ("/search/truncates", test_search_truncates);
      ADD_TEST ("/search/restart", test_search_restart);
    }

#undef ADD_TEST

  return g_test_run ();
}
