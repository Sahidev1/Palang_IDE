/* test-lsp.c — tests for the language server client and manager.
 *
 * The server is tests/fake-lsp-server (its path comes from meson as
 * FAKE_LSP_SERVER), which logs every message it receives to a file; the
 * tests read that log to see what Palang sent.
 */

#include "config.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <jsonrpc-glib.h>
#include <string.h>

#include "palang-lsp-client.h"
#include "palang-lsp-completion-item.h"
#include "palang-lsp-languages.h"
#include "palang-lsp-manager.h"
#include "palang-lsp-markdown.h"
#include "palang-lsp-snippet.h"
#include "palang-lsp-position.h"

typedef struct {
  char  *dir;
  char  *log_path;   /* the fake server's log */
  GFile *folder;     /* the "project": the temporary folder */

  /* What the client or manager under test reported. */
  guint  n_ready;
  guint  n_failed;
  char  *failed_message;
  gboolean finalized;  /* the client object is gone */
} Fixture;

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  fixture->dir = g_dir_make_tmp ("palang-test-XXXXXX", NULL);
  fixture->log_path = g_build_filename (fixture->dir, "server.log", NULL);
  fixture->folder = g_file_new_for_path (fixture->dir);
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GDir) dir = g_dir_open (fixture->dir, 0, NULL);
  const char *name;

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *path = g_build_filename (fixture->dir, name, NULL);
      g_unlink (path);
    }
  g_rmdir (fixture->dir);

  g_free (fixture->dir);
  g_free (fixture->log_path);
  g_clear_object (&fixture->folder);
  g_free (fixture->failed_message);
}

/* ---- Helpers ------------------------------------------------------------ */

static void
on_ready (PalangLspClient *client,
          Fixture         *fixture)
{
  fixture->n_ready++;
}

static void
on_failed (GObject    *client_or_manager,
           const char *message,
           Fixture    *fixture)
{
  fixture->n_failed++;
  g_free (fixture->failed_message);
  fixture->failed_message = g_strdup (message);
}

static void
on_finalized (gpointer  data,
              GObject  *where_the_object_was)
{
  Fixture *fixture = data;

  fixture->finalized = TRUE;
}

/* Runs the main loop until *@counter is at least @value (10 s at most). */
static void
wait_for_count (guint      *counter,
                guint       value,
                const char *what)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

  while (*counter < value)
    {
      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out waiting for %s", what);
      g_main_context_iteration (NULL, FALSE);
    }
}

static void
wait_for_flag (gboolean   *flag,
               const char *what)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

  while (!*flag)
    {
      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out waiting for %s", what);
      g_main_context_iteration (NULL, FALSE);
    }
}

/* Returns: (transfer full): the fake server's log, "" if there is none. */
static char *
read_log (Fixture *fixture)
{
  char *contents = NULL;

  if (!g_file_get_contents (fixture->log_path, &contents, NULL, NULL))
    return g_strdup ("");

  return contents;
}

/* Runs the main loop until the log has a line starting with @method. */
static void
wait_for_log (Fixture    *fixture,
              const char *method)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
  g_autofree char *line_start = g_strdup_printf ("%s ", method);

  for (;;)
    {
      g_autofree char *log = read_log (fixture);
      g_auto (GStrv) lines = g_strsplit (log, "\n", -1);

      for (guint i = 0; lines[i] != NULL; i++)
        if (g_str_has_prefix (lines[i], line_start))
          return;

      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out waiting for “%s” in the server log:\n%s", method, log);
      g_main_context_iteration (NULL, FALSE);
    }
}

/* Runs the main loop until the file at @path contains @text. */
static void
wait_for_text (const char *path,
               const char *text)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

  for (;;)
    {
      g_autofree char *contents = NULL;

      if (g_file_get_contents (path, &contents, NULL, NULL) && strstr (contents, text) != NULL)
        return;
      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out waiting for “%s” in %s", text, path);
      g_main_context_iteration (NULL, FALSE);
    }
}

/* Returns: how many lines of the log start with @method. */
static guint
count_in_log (Fixture    *fixture,
              const char *method)
{
  g_autofree char *log = read_log (fixture);
  g_auto (GStrv) lines = g_strsplit (log, "\n", -1);
  g_autofree char *line_start = g_strdup_printf ("%s ", method);
  guint count = 0;

  for (guint i = 0; lines[i] != NULL; i++)
    if (g_str_has_prefix (lines[i], line_start))
      count++;

  return count;
}

/* Returns: (transfer full): a client for the fake server, with @option
 * (nullable) added to its command line, signals connected to @fixture. */
static PalangLspClient *
new_fake_client (Fixture    *fixture,
                 GFile      *root,
                 const char *option)
{
  const char *argv[] = { FAKE_LSP_SERVER, "--log", fixture->log_path, option, NULL };
  PalangLspClient *client = palang_lsp_client_new ("Fake", argv, root, NULL);

  g_signal_connect (client, "ready", G_CALLBACK (on_ready), fixture);
  g_signal_connect (client, "failed", G_CALLBACK (on_failed), fixture);
  g_object_weak_ref (G_OBJECT (client), on_finalized, fixture);

  return client;
}

/* Stops @client, drops our reference and waits until the server has
 * exited (the client lives until then). */
static void
stop_and_wait (Fixture         *fixture,
               PalangLspClient *client)
{
  palang_lsp_client_stop (client);
  g_object_unref (client);
  wait_for_flag (&fixture->finalized, "the client to finish stopping");
}

/* ---- Languages ---------------------------------------------------------- */

static void
test_languages (void)
{
  g_autofree char *setting = NULL;

  g_assert_cmpstr (palang_lsp_find_server ("c")->key, ==, "c");
  g_assert_cmpstr (palang_lsp_find_server ("chdr")->key, ==, "c");
  g_assert_cmpstr (palang_lsp_find_server ("cpp")->key, ==, "c");
  g_assert_cmpstr (palang_lsp_find_server ("python3")->key, ==, "python");
  g_assert_cmpstr (palang_lsp_find_server ("js")->key, ==, "javascript");
  g_assert_null (palang_lsp_find_server ("markdown"));
  g_assert_null (palang_lsp_find_server (NULL));

  g_assert_cmpstr (palang_lsp_get_language_id ("chdr"), ==, "c");
  g_assert_cmpstr (palang_lsp_get_language_id ("cpp"), ==, "cpp");
  g_assert_cmpstr (palang_lsp_get_language_id ("python3"), ==, "python");
  g_assert_cmpstr (palang_lsp_get_language_id ("js"), ==, "javascript");
  g_assert_null (palang_lsp_get_language_id ("markdown"));

  setting = palang_lsp_dup_setting_name (palang_lsp_find_server ("python3"));
  g_assert_cmpstr (setting, ==, "python-server");
}

/* ---- Client ------------------------------------------------------------- */

/* The handshake and the polite goodbye, with the folder sent as root. */
static void
test_client_start_and_stop (Fixture *fixture, gconstpointer data)
{
  PalangLspClient *client = new_fake_client (fixture, fixture->folder, NULL);
  g_autofree char *root_uri = g_file_get_uri (fixture->folder);
  g_autofree char *log = NULL;

  g_assert_false (palang_lsp_client_is_ready (client));
  palang_lsp_client_start (client);
  wait_for_count (&fixture->n_ready, 1, "ready");

  g_assert_true (palang_lsp_client_is_ready (client));
  g_assert_nonnull (palang_lsp_client_get_capabilities (client));
  wait_for_log (fixture, "initialized");

  log = read_log (fixture);
  g_assert_nonnull (strstr (log, root_uri));
  g_assert_nonnull (strstr (log, "\"workspaceFolders\":[{"));
  g_assert_nonnull (strstr (log, "\"name\":\"Palang\""));

  stop_and_wait (fixture, client);
  g_assert_cmpuint (count_in_log (fixture, "shutdown"), ==, 1);
  g_assert_cmpuint (count_in_log (fixture, "exit"), ==, 1);
  g_assert_cmpuint (fixture->n_failed, ==, 0);
}

/* Without a folder, the root is null. */
static void
test_client_no_folder (Fixture *fixture, gconstpointer data)
{
  PalangLspClient *client = new_fake_client (fixture, NULL, NULL);
  g_autofree char *log = NULL;

  palang_lsp_client_start (client);
  wait_for_count (&fixture->n_ready, 1, "ready");

  log = read_log (fixture);
  g_assert_nonnull (strstr (log, "\"rootUri\":null"));
  g_assert_null (strstr (log, "workspaceFolders\":["));

  stop_and_wait (fixture, client);
}

static void
on_echo_replied (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GVariant **reply = user_data;
  g_autoptr (GError) error = NULL;

  *reply = palang_lsp_client_call_finish (PALANG_LSP_CLIENT (source), result, &error);
  g_assert_no_error (error);
}

static void
on_not_connected (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = palang_lsp_client_call_finish (PALANG_LSP_CLIENT (source),
                                                              result, &error);

  g_assert_null (reply);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED);
  *done = TRUE;
}

/* A request before the server is ready fails; once ready, it is answered. */
static void
test_client_call (Fixture *fixture, gconstpointer data)
{
  PalangLspClient *client = new_fake_client (fixture, NULL, NULL);
  g_autoptr (GVariant) params = JSONRPC_MESSAGE_NEW ("word", JSONRPC_MESSAGE_PUT_STRING ("hello"));
  g_autoptr (GVariant) reply = NULL;
  gboolean not_connected = FALSE;
  const char *word = NULL;

  palang_lsp_client_call_async (client, "fake/echo", params, NULL, on_not_connected, &not_connected);
  wait_for_flag (&not_connected, "the not-connected error");

  palang_lsp_client_start (client);
  wait_for_count (&fixture->n_ready, 1, "ready");

  palang_lsp_client_call_async (client, "fake/echo", params, NULL, on_echo_replied, &reply);
  while (reply == NULL)
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (g_variant_lookup (reply, "word", "&s", &word));
  g_assert_cmpstr (word, ==, "hello");

  stop_and_wait (fixture, client);
}

/* A server given by its full path gets its own folder first on PATH, so
 * it finds programs installed next to it (nvm's `node`). */
static void
test_client_path_includes_server_folder (Fixture *fixture, gconstpointer data)
{
  PalangLspClient *client = new_fake_client (fixture, NULL, NULL);
  g_autoptr (GVariant) params = JSONRPC_MESSAGE_NEW ("name", JSONRPC_MESSAGE_PUT_STRING ("PATH"));
  g_autoptr (GVariant) reply = NULL;
  g_autofree char *folder = g_path_get_dirname (FAKE_LSP_SERVER);
  g_autofree char *expected_start = g_strconcat (folder, ":", NULL);

  palang_lsp_client_start (client);
  wait_for_count (&fixture->n_ready, 1, "ready");

  palang_lsp_client_call_async (client, "fake/getenv", params, NULL, on_echo_replied, &reply);
  while (reply == NULL)
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (g_str_has_prefix (g_variant_get_string (reply, NULL), expected_start));

  stop_and_wait (fixture, client);
}

/* A program that doesn't exist: "failed", with the install hint. */
static void
test_client_missing_program (Fixture *fixture, gconstpointer data)
{
  const char *argv[] = { "palang-test-no-such-server", NULL };
  g_autoptr (PalangLspClient) client =
    palang_lsp_client_new ("Fake", argv, NULL, "sudo apt install fake-server");

  g_signal_connect (client, "ready", G_CALLBACK (on_ready), fixture);
  g_signal_connect (client, "failed", G_CALLBACK (on_failed), fixture);

  /* Spawning fails right away, so "failed" comes before start returns. */
  palang_lsp_client_start (client);
  g_assert_cmpuint (fixture->n_failed, ==, 1);
  g_assert_cmpuint (fixture->n_ready, ==, 0);
  g_assert_nonnull (strstr (fixture->failed_message, "isn't installed"));
  g_assert_nonnull (strstr (fixture->failed_message, "sudo apt install fake-server"));

  /* Stopping a failed client is fine and doesn't need waiting. */
  palang_lsp_client_stop (client);
}

/* A server that crashes is started again three times, then given up. */
static void
test_client_restarts_then_gives_up (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangLspClient) client = new_fake_client (fixture, NULL, "--crash-after-initialized");

  palang_lsp_client_start (client);
  wait_for_count (&fixture->n_failed, 1, "failed");

  /* The first start and three restarts, each one got ready. */
  g_assert_cmpuint (fixture->n_ready, ==, 4);
  g_assert_cmpuint (count_in_log (fixture, "initialize"), ==, 4);
  g_assert_false (palang_lsp_client_is_ready (client));
  g_assert_nonnull (strstr (fixture->failed_message, "keeps stopping"));
}

/* A server that answers "initialize" with an error isn't started again
 * (it would answer the same); its message is passed on. */
static void
test_client_initialize_refused (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangLspClient) client = new_fake_client (fixture, NULL, "--refuse-initialize");

  palang_lsp_client_start (client);
  wait_for_count (&fixture->n_failed, 1, "failed");

  g_assert_cmpuint (fixture->n_ready, ==, 0);
  g_assert_cmpuint (count_in_log (fixture, "initialize"), ==, 1);
  g_assert_nonnull (strstr (fixture->failed_message, "Something it needs is missing"));
}

/* A server that ignores "shutdown" is killed after two seconds. */
static void
test_client_kills_hanging_server (Fixture *fixture, gconstpointer data)
{
  PalangLspClient *client = new_fake_client (fixture, NULL, "--ignore-shutdown");
  gint64 start;

  palang_lsp_client_start (client);
  wait_for_count (&fixture->n_ready, 1, "ready");

  start = g_get_monotonic_time ();
  stop_and_wait (fixture, client);
  g_assert_cmpint (g_get_monotonic_time () - start, >=, 2 * G_USEC_PER_SEC - 100000);
  g_assert_cmpuint (count_in_log (fixture, "shutdown"), ==, 1);
}

/* ---- Manager ------------------------------------------------------------ */

/* Returns: (transfer full): the command line that starts the fake server
 * logging to @log_path, with @option (nullable) added. */
static char *
dup_fake_command (const char *log_path,
                  const char *option)
{
  g_autofree char *quoted_server = g_shell_quote (FAKE_LSP_SERVER);
  g_autofree char *quoted_log = g_shell_quote (log_path);

  return g_strdup_printf ("%s --log %s %s", quoted_server, quoted_log,
                          option != NULL ? option : "");
}

/* Returns: (transfer full): settings in the fixture's folder, with
 * c-server set to the fake server (logging to @log_path, with @option
 * added if not NULL) and the Python server turned off. */
static PalangSettings *
new_settings (Fixture    *fixture,
              const char *log_path,
              const char *option)
{
  g_autofree char *path = g_build_filename (fixture->dir, "config.ini", NULL);
  g_autofree char *command = dup_fake_command (log_path, option);
  PalangSettings *settings = palang_settings_new_for_path (path);

  g_assert_true (palang_settings_set_from_string (settings, "c-server", command, NULL));
  g_assert_true (palang_settings_set_from_string (settings, "python-server", "", NULL));

  return settings;
}

/* Returns: (transfer full): an empty document in the language @language_id. */
static PalangDocument *
new_document (const char *language_id)
{
  PalangDocument *document = palang_document_new ();
  GtkSourceLanguageManager *languages = gtk_source_language_manager_get_default ();

  gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (document),
                                  gtk_source_language_manager_get_language (languages, language_id));
  return document;
}

/* Waits until the manager's server for @document is ready. */
static void
wait_until_ready (PalangLspManager *manager,
                  PalangDocument   *document)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

  for (;;)
    {
      PalangLspClient *client = palang_lsp_manager_get_client (manager, document);

      if (client != NULL && palang_lsp_client_is_ready (client))
        return;
      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out waiting for the manager's server");
      g_main_context_iteration (NULL, FALSE);
    }
}

/* Stops the manager's servers and waits until @client is gone. */
static void
dispose_manager_and_wait (Fixture          *fixture,
                          PalangLspManager *manager,
                          PalangLspClient  *client)
{
  fixture->finalized = FALSE;
  g_object_weak_ref (G_OBJECT (client), on_finalized, fixture);
  g_object_unref (manager);
  wait_for_flag (&fixture->finalized, "the manager's server to stop");
}

/* One server per server key: a .c and a .h document share clangd's
 * stand-in; Python's server is turned off; Markdown has none. */
static void
test_manager_one_server_per_language (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) c_file = new_document ("c");
  g_autoptr (PalangDocument) header = new_document ("chdr");
  g_autoptr (PalangDocument) python_file = new_document ("python3");
  g_autoptr (PalangDocument) markdown_file = new_document ("markdown");
  PalangLspClient *client;

  palang_lsp_manager_set_folder (manager, fixture->folder);
  palang_lsp_manager_add_document (manager, c_file);
  palang_lsp_manager_add_document (manager, header);
  palang_lsp_manager_add_document (manager, python_file);
  palang_lsp_manager_add_document (manager, markdown_file);
  wait_until_ready (manager, c_file);

  client = palang_lsp_manager_get_client (manager, c_file);
  g_assert_true (palang_lsp_manager_get_client (manager, header) == client);
  g_assert_null (palang_lsp_manager_get_client (manager, python_file));
  g_assert_null (palang_lsp_manager_get_client (manager, markdown_file));
  g_assert_cmpuint (count_in_log (fixture, "initialize"), ==, 1);

  /* A document that gets a language later (Save As main.py → main.c). */
  gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (markdown_file),
                                  gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (c_file)));
  g_assert_true (palang_lsp_manager_get_client (manager, markdown_file) == client);

  palang_lsp_manager_remove_document (manager, header);
  dispose_manager_and_wait (fixture, manager, client);
  g_assert_cmpuint (count_in_log (fixture, "exit"), ==, 1);
}

/* Changing the server's setting starts the new command; a new folder
 * starts the server again with that root. */
static void
test_manager_restarts_on_changes (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) c_file = new_document ("c");
  g_autofree char *second_log = g_build_filename (fixture->dir, "second.log", NULL);
  g_autofree char *command = dup_fake_command (second_log, NULL);
  g_autoptr (GFile) subfolder = g_file_get_child (fixture->folder, "sub");
  g_autofree char *subfolder_path = g_file_get_path (subfolder);
  g_autofree char *subfolder_uri = g_file_get_uri (subfolder);
  PalangLspClient *first_client;

  /* The server runs in the folder, so it must exist. */
  g_mkdir (subfolder_path, 0700);

  palang_lsp_manager_add_document (manager, c_file);
  wait_until_ready (manager, c_file);
  first_client = palang_lsp_manager_get_client (manager, c_file);

  /* New command: the first server is asked to leave, the second starts. */
  g_assert_true (palang_settings_set_from_string (settings, "c-server", command, NULL));
  g_assert_true (palang_lsp_manager_get_client (manager, c_file) != first_client);
  wait_until_ready (manager, c_file);
  wait_for_log (fixture, "exit");

  /* New folder: started again, with the folder as root. */
  g_object_set (manager, "folder", subfolder, NULL);
  wait_for_text (second_log, subfolder_uri);
  wait_until_ready (manager, c_file);

  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, c_file));
  g_unlink (second_log);
  g_rmdir (subfolder_path);
}

/* A missing server is reported once, not for every document; restarting
 * tries again (and reports again). */
static void
test_manager_reports_missing_server_once (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  g_autoptr (PalangLspManager) manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) first = new_document ("c");
  g_autoptr (PalangDocument) second = new_document ("cpp");

  g_assert_true (palang_settings_set_from_string (settings, "c-server",
                                                  "palang-test-no-such-server", NULL));
  g_signal_connect (manager, "server-failed", G_CALLBACK (on_failed), fixture);

  palang_lsp_manager_add_document (manager, first);
  palang_lsp_manager_add_document (manager, second);
  g_assert_cmpuint (fixture->n_failed, ==, 1);
  /* Not the default command, so no install hint for clangd. */
  g_assert_nonnull (strstr (fixture->failed_message, "wasn't found"));

  palang_lsp_manager_restart (manager);
  g_assert_cmpuint (fixture->n_failed, ==, 2);
}


/* ---- Positions ---------------------------------------------------------- */

/* "aé😀b": a, é and b are one UTF-16 unit each, 😀 is two. */
static void
test_position_utf16 (void)
{
  const char *text = "a\xc3\xa9\xf0\x9f\x98\x80" "b";

  g_assert_cmpuint (palang_lsp_utf16_length (""), ==, 0);
  g_assert_cmpuint (palang_lsp_utf16_length (text), ==, 5);

  g_assert_cmpuint (palang_lsp_chars_for_utf16 (text, 0), ==, 0);
  g_assert_cmpuint (palang_lsp_chars_for_utf16 (text, 2), ==, 2);  /* before 😀 */
  g_assert_cmpuint (palang_lsp_chars_for_utf16 (text, 3), ==, 2);  /* inside 😀 */
  g_assert_cmpuint (palang_lsp_chars_for_utf16 (text, 4), ==, 3);  /* before b */
  g_assert_cmpuint (palang_lsp_chars_for_utf16 (text, 5), ==, 4);
  g_assert_cmpuint (palang_lsp_chars_for_utf16 (text, 99), ==, 4); /* past the end */
}

static void
test_position_iters (void)
{
  g_autoptr (GtkTextBuffer) buffer = gtk_text_buffer_new (NULL);
  GtkTextIter iter;
  guint line, character;

  gtk_text_buffer_set_text (buffer, "x\na\xf0\x9f\x98\x80" "b\n", -1);

  /* The b: character 2 for GTK, 3 for LSP. */
  gtk_text_buffer_get_iter_at_line_offset (buffer, &iter, 1, 2);
  palang_lsp_position_from_iter (&iter, &line, &character);
  g_assert_cmpuint (line, ==, 1);
  g_assert_cmpuint (character, ==, 3);

  palang_lsp_iter_from_position (buffer, 1, 3, &iter);
  g_assert_cmpint (gtk_text_iter_get_line (&iter), ==, 1);
  g_assert_cmpint (gtk_text_iter_get_line_offset (&iter), ==, 2);

  /* Past the end of the line: the line's end. */
  palang_lsp_iter_from_position (buffer, 0, 50, &iter);
  g_assert_cmpint (gtk_text_iter_get_line (&iter), ==, 0);
  g_assert_cmpint (gtk_text_iter_get_line_offset (&iter), ==, 1);

  /* Past the last line: the buffer's end. */
  palang_lsp_iter_from_position (buffer, 9, 0, &iter);
  g_assert_true (gtk_text_iter_is_end (&iter));
}

/* ---- Document sync ------------------------------------------------------ */

/* Loading finished; it must have worked. */
static void
on_loaded (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  palang_document_load_finish (PALANG_DOCUMENT (source), result, &error);
  g_assert_no_error (error);
  *done = TRUE;
}

/* Saving finished; it must have worked. */
static void
on_saved (GObject      *source,
          GAsyncResult *result,
          gpointer      user_data)
{
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  palang_document_save_finish (PALANG_DOCUMENT (source), result, &error);
  g_assert_no_error (error);
  *done = TRUE;
}

/* Runs the main loop until the log has @count lines starting with @method. */
static void
wait_for_log_count (Fixture    *fixture,
                    const char *method,
                    guint       count)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

  while (count_in_log (fixture, method) < count)
    {
      if (g_get_monotonic_time () > deadline)
        {
          g_autofree char *log = read_log (fixture);

          g_error ("Timed out waiting for %u × “%s” in the server log:\n%s", count, method, log);
        }
      g_main_context_iteration (NULL, FALSE);
    }
}

/* Returns: (transfer full): the params of the @n-th (from 0) message
 * @method in the log, parsed as JSON. */
static JsonNode *
get_logged_params (Fixture    *fixture,
                   const char *method,
                   guint       n)
{
  g_autofree char *log = read_log (fixture);
  g_auto (GStrv) lines = g_strsplit (log, "\n", -1);
  g_autofree char *line_start = g_strdup_printf ("%s ", method);
  g_autoptr (GError) error = NULL;

  for (guint i = 0; lines[i] != NULL; i++)
    if (g_str_has_prefix (lines[i], line_start) && n-- == 0)
      {
        JsonNode *node = json_from_string (lines[i] + strlen (line_start), &error);

        g_assert_no_error (error);
        return node;
      }

  g_error ("No message %s number %u in the log:\n%s", method, n, log);
}

/* Returns: (transfer full): all values at @path (a JsonPath expression
 * like "$.textDocument.uri") in @params, as a JSON array node. */
static JsonNode *
query (JsonNode   *params,
       const char *path)
{
  g_autoptr (GError) error = NULL;
  JsonNode *matches = json_path_query (path, params, &error);

  g_assert_no_error (error);
  return matches;
}

/* Returns: the number at @path in @params, which must be there once. */
static gint64
get_int (JsonNode   *params,
         const char *path)
{
  g_autoptr (JsonNode) matches = query (params, path);
  JsonArray *array = json_node_get_array (matches);

  g_assert_cmpuint (json_array_get_length (array), ==, 1);
  return json_array_get_int_element (array, 0);
}

/* Returns: (transfer full): the string at @path in @params, which must be
 * there once. */
static char *
dup_string (JsonNode   *params,
            const char *path)
{
  g_autoptr (JsonNode) matches = query (params, path);
  JsonArray *array = json_node_get_array (matches);

  g_assert_cmpuint (json_array_get_length (array), ==, 1);
  return g_strdup (json_array_get_string_element (array, 0));
}

/* Returns: TRUE if there is anything at @path in @params. */
static gboolean
has_member (JsonNode   *params,
            const char *path)
{
  g_autoptr (JsonNode) matches = query (params, path);

  return json_array_get_length (json_node_get_array (matches)) > 0;
}

/* Returns: (transfer full): a document loaded from a new file @name with
 * @text in the fixture's folder. */
static PalangDocument *
load_new_file (Fixture    *fixture,
               const char *name,
               const char *text)
{
  g_autofree char *path = g_build_filename (fixture->dir, name, NULL);
  g_autoptr (GFile) file = g_file_new_for_path (path);
  PalangDocument *document = palang_document_new ();
  gboolean loaded = FALSE;

  g_assert_true (g_file_set_contents (path, text, -1, NULL));
  palang_document_load_async (document, file, NULL, on_loaded, &loaded);
  wait_for_flag (&loaded, "loading the file");
  return document;
}

/* Saves @document (to @target if not NULL) and waits until it is done. */
static void
save_document (PalangDocument *document,
               GFile          *target)
{
  gboolean saved = FALSE;

  palang_document_save_async (document, target, FALSE, NULL, on_saved, &saved);
  wait_for_flag (&saved, "saving");
}

/* Inserts @text at character @offset of @line. */
static void
insert_at (PalangDocument *document,
           int             line,
           int             offset,
           const char     *text)
{
  GtkTextIter iter;

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, line, offset);
  gtk_text_buffer_insert (GTK_TEXT_BUFFER (document), &iter, text, -1);
}

/* didOpen with the whole text; edits as ranges counted in UTF-16;
 * didSave; didClose when the document goes. */
static void
test_sync_open_edit_close (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", "int a;\n");
  g_autofree char *uri = g_file_get_uri (palang_document_get_file (document));
  g_autoptr (JsonNode) opened = NULL;
  g_autoptr (JsonNode) emoji = NULL;
  g_autoptr (JsonNode) after_emoji = NULL;
  g_autoptr (JsonNode) deleted = NULL;
  g_autofree char *opened_uri = NULL;
  g_autofree char *language_id = NULL;
  g_autofree char *text = NULL;
  g_autofree char *inserted = NULL;
  g_autofree char *deleted_text = NULL;
  PalangLspClient *client;
  GtkTextIter start, end;

  palang_lsp_manager_set_folder (manager, fixture->folder);
  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  opened = get_logged_params (fixture, "textDocument/didOpen", 0);
  opened_uri = dup_string (opened, "$.textDocument.uri");
  language_id = dup_string (opened, "$.textDocument.languageId");
  text = dup_string (opened, "$.textDocument.text");
  g_assert_cmpstr (opened_uri, ==, uri);
  g_assert_cmpstr (language_id, ==, "c");
  g_assert_cmpstr (text, ==, "int a;\n");
  g_assert_cmpint (get_int (opened, "$.textDocument.version"), ==, 1);

  /* "int 😀a;": inserted at character 4. */
  insert_at (document, 0, 4, "\xf0\x9f\x98\x80");
  /* "int 😀xa;": GTK offset 5, but LSP character 6 (😀 is two units). */
  insert_at (document, 0, 5, "x");
  /* Delete the emoji again: LSP range 4..6. */
  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &start, 0, 4);
  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &end, 0, 5);
  gtk_text_buffer_delete (GTK_TEXT_BUFFER (document), &start, &end);
  wait_for_log_count (fixture, "textDocument/didChange", 3);

  emoji = get_logged_params (fixture, "textDocument/didChange", 0);
  inserted = dup_string (emoji, "$.contentChanges[0].text");
  g_assert_cmpstr (inserted, ==, "\xf0\x9f\x98\x80");
  g_assert_cmpint (get_int (emoji, "$.contentChanges[0].range.start.character"), ==, 4);
  g_assert_cmpint (get_int (emoji, "$.contentChanges[0].range.end.character"), ==, 4);
  g_assert_cmpint (get_int (emoji, "$.textDocument.version"), ==, 2);

  after_emoji = get_logged_params (fixture, "textDocument/didChange", 1);
  g_assert_cmpint (get_int (after_emoji, "$.contentChanges[0].range.start.character"), ==, 6);

  deleted = get_logged_params (fixture, "textDocument/didChange", 2);
  deleted_text = dup_string (deleted, "$.contentChanges[0].text");
  g_assert_cmpstr (deleted_text, ==, "");
  g_assert_cmpint (get_int (deleted, "$.contentChanges[0].range.start.character"), ==, 4);
  g_assert_cmpint (get_int (deleted, "$.contentChanges[0].range.end.character"), ==, 6);
  g_assert_cmpint (get_int (deleted, "$.textDocument.version"), ==, 4);
  g_assert_cmpint (palang_lsp_document_get_version (palang_lsp_manager_get_lsp_document (manager, document)),
                   ==, 4);

  save_document (document, NULL);
  wait_for_log (fixture, "textDocument/didSave");

  client = palang_lsp_manager_get_client (manager, document);
  palang_lsp_manager_remove_document (manager, document);
  wait_for_log (fixture, "textDocument/didClose");

  dispose_manager_and_wait (fixture, manager, client);
}

/* A server that wants the whole text gets it, without a range. */
static void
test_sync_full_text (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, "--full-sync");
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", "int a;\n");
  g_autoptr (JsonNode) changed = NULL;
  g_autofree char *text = NULL;

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  insert_at (document, 1, 0, "int b;\n");
  wait_for_log (fixture, "textDocument/didChange");

  changed = get_logged_params (fixture, "textDocument/didChange", 0);
  text = dup_string (changed, "$.contentChanges[0].text");
  g_assert_cmpstr (text, ==, "int a;\nint b;\n");
  g_assert_false (has_member (changed, "$.contentChanges[0].range"));

  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* An untitled document is sent once it has a file; Save As to another
 * name closes the old name and opens the new one. */
static void
test_sync_untitled_and_save_as (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = new_document ("c");
  g_autofree char *first_path = g_build_filename (fixture->dir, "first.c", NULL);
  g_autofree char *second_path = g_build_filename (fixture->dir, "second.c", NULL);
  g_autoptr (GFile) first = g_file_new_for_path (first_path);
  g_autoptr (GFile) second = g_file_new_for_path (second_path);
  g_autofree char *first_uri = g_file_get_uri (first);
  g_autofree char *second_uri = g_file_get_uri (second);
  g_autoptr (JsonNode) opened = NULL;
  g_autoptr (JsonNode) reopened = NULL;
  g_autoptr (JsonNode) closed = NULL;
  g_autofree char *opened_uri = NULL;
  g_autofree char *reopened_uri = NULL;
  g_autofree char *closed_uri = NULL;

  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (document), "int a;\n", -1);
  palang_lsp_manager_add_document (manager, document);
  wait_until_ready (manager, document);
  g_assert_false (palang_lsp_document_is_open (palang_lsp_manager_get_lsp_document (manager, document)));

  save_document (document, first);
  wait_for_log (fixture, "textDocument/didOpen");
  opened = get_logged_params (fixture, "textDocument/didOpen", 0);
  opened_uri = dup_string (opened, "$.textDocument.uri");
  g_assert_cmpstr (opened_uri, ==, first_uri);

  save_document (document, second);
  wait_for_log_count (fixture, "textDocument/didOpen", 2);
  closed = get_logged_params (fixture, "textDocument/didClose", 0);
  closed_uri = dup_string (closed, "$.textDocument.uri");
  reopened = get_logged_params (fixture, "textDocument/didOpen", 1);
  reopened_uri = dup_string (reopened, "$.textDocument.uri");
  g_assert_cmpstr (closed_uri, ==, first_uri);
  g_assert_cmpstr (reopened_uri, ==, second_uri);

  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
  g_unlink (first_path);
  g_unlink (second_path);
}

/* After a crash the restarted server gets the document again, with the
 * text as it is now. */
static void
test_sync_reopens_after_restart (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, "--crash-on-change");
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", "int a;\n");
  g_autoptr (JsonNode) reopened = NULL;
  g_autofree char *text = NULL;

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  insert_at (document, 1, 0, "int b;\n");  /* the server crashes on this */
  wait_for_log_count (fixture, "textDocument/didOpen", 2);

  reopened = get_logged_params (fixture, "textDocument/didOpen", 1);
  text = dup_string (reopened, "$.textDocument.text");
  g_assert_cmpstr (text, ==, "int a;\nint b;\n");
  g_assert_cmpint (get_int (reopened, "$.textDocument.version"), ==, 3);

  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* ---- Diagnostics -------------------------------------------------------- */

/* Runs the main loop until @lsp_document shows @count diagnostics. */
static void
wait_for_diagnostics (PalangLspDocument *lsp_document,
                      guint              count)
{
  gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

  while (palang_lsp_document_get_n_diagnostics (lsp_document) != count)
    {
      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out waiting for %u diagnostics (have %u)", count,
                 palang_lsp_document_get_n_diagnostics (lsp_document));
      g_main_context_iteration (NULL, FALSE);
    }
}

/* Returns: TRUE if the character at @line, @offset has the tag @name. */
static gboolean
has_tag_at (PalangDocument *document,
            int             line,
            int             offset,
            const char     *name)
{
  GtkTextTagTable *table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (document));
  GtkTextTag *tag = gtk_text_tag_table_lookup (table, name);
  GtkTextIter iter;

  if (tag == NULL)
    return FALSE;

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, line, offset);
  return gtk_text_iter_has_tag (&iter, tag);
}

/* Returns: TRUE if @line has a gutter mark of @category. */
static gboolean
has_gutter_mark (PalangDocument *document,
                 int             line,
                 const char     *category)
{
  GSList *marks = gtk_source_buffer_get_source_marks_at_line (GTK_SOURCE_BUFFER (document),
                                                             line, category);
  gboolean found = marks != NULL;

  g_slist_free (marks);
  return found;
}

/* The server's errors and warnings are underlined at the right characters
 * (an emoji before them counts twice for the server), get gutter icons and
 * messages, follow edits, and go away with the document. */
static void
test_diagnostics_shown_and_updated (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path,
                                                      "--full-sync --diagnose");
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  /* Line 1: 😀 is GTK character 0; "meh" starts at 2, "bad" at 6. */
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c",
                                                       "int bad;\n\xf0\x9f\x98\x80 meh bad\n");
  PalangLspDocument *lsp_document;
  PalangLspClient *client;
  g_auto (GStrv) messages = NULL;
  GtkTextIter iter, start, end;

  palang_lsp_manager_add_document (manager, document);
  lsp_document = palang_lsp_manager_get_lsp_document (manager, document);
  wait_for_diagnostics (lsp_document, 3);

  g_assert_true (has_tag_at (document, 0, 4, "palang-lsp-error"));
  g_assert_false (has_tag_at (document, 0, 3, "palang-lsp-error"));
  g_assert_true (has_tag_at (document, 1, 2, "palang-lsp-warning"));
  g_assert_true (has_tag_at (document, 1, 6, "palang-lsp-error"));
  g_assert_true (has_tag_at (document, 1, 8, "palang-lsp-error"));
  g_assert_false (has_tag_at (document, 1, 0, "palang-lsp-error"));
  g_assert_false (has_tag_at (document, 1, 5, "palang-lsp-error"));

  g_assert_true (has_gutter_mark (document, 0, "palang-lsp-error"));
  g_assert_true (has_gutter_mark (document, 1, "palang-lsp-error"));
  g_assert_true (has_gutter_mark (document, 1, "palang-lsp-warning"));

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, 1, 7);
  messages = palang_lsp_document_dup_messages_at (lsp_document, &iter);
  g_assert_cmpstrv (messages, ((const char *[]) { "Error: bad (fake)", NULL }));
  g_clear_pointer (&messages, g_strfreev);

  /* Just after "bad" (the end of its range) still counts; one more
   * character doesn't. */
  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, 1, 9);
  messages = palang_lsp_document_dup_messages_at (lsp_document, &iter);
  g_assert_cmpstrv (messages, ((const char *[]) { "Error: bad (fake)", NULL }));
  g_clear_pointer (&messages, g_strfreev);
  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, 0, 1);
  g_assert_null (palang_lsp_document_dup_messages_at (lsp_document, &iter));

  /* Delete the first "bad": its error goes. */
  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &start, 0, 4);
  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &end, 0, 7);
  gtk_text_buffer_delete (GTK_TEXT_BUFFER (document), &start, &end);
  wait_for_diagnostics (lsp_document, 2);
  g_assert_false (has_gutter_mark (document, 0, "palang-lsp-error"));
  g_assert_true (has_tag_at (document, 1, 6, "palang-lsp-error"));

  /* Closing the document removes what was shown. */
  client = palang_lsp_manager_get_client (manager, document);
  palang_lsp_manager_remove_document (manager, document);
  g_assert_false (has_tag_at (document, 1, 6, "palang-lsp-error"));
  g_assert_false (has_gutter_mark (document, 1, "palang-lsp-error"));

  dispose_manager_and_wait (fixture, manager, client);
}

/* Diagnostics computed for an older version of the text are dropped. */
static void
test_diagnostics_stale_dropped (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", "int a;\n");
  g_autoptr (GVariant) diagnostics = JSONRPC_MESSAGE_NEW_ARRAY (
    "{",
      "range", "{",
        "start", "{", "line", JSONRPC_MESSAGE_PUT_INT64 (0), "character", JSONRPC_MESSAGE_PUT_INT64 (4), "}",
        "end", "{", "line", JSONRPC_MESSAGE_PUT_INT64 (0), "character", JSONRPC_MESSAGE_PUT_INT64 (5), "}",
      "}",
      "message", JSONRPC_MESSAGE_PUT_STRING ("about a"),
    "}");
  PalangLspDocument *lsp_document;
  gint64 version;

  palang_lsp_manager_add_document (manager, document);
  lsp_document = palang_lsp_manager_get_lsp_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");
  version = palang_lsp_document_get_version (lsp_document);

  palang_lsp_document_set_diagnostics (lsp_document, version - 1, diagnostics);
  g_assert_cmpuint (palang_lsp_document_get_n_diagnostics (lsp_document), ==, 0);

  palang_lsp_document_set_diagnostics (lsp_document, version, diagnostics);
  g_assert_cmpuint (palang_lsp_document_get_n_diagnostics (lsp_document), ==, 1);
  /* No severity given: shown as an error. */
  g_assert_true (has_tag_at (document, 0, 4, "palang-lsp-error"));

  /* Without a version (pylsp sends none), they are always shown. */
  palang_lsp_document_set_diagnostics (lsp_document, -1, diagnostics);
  g_assert_cmpuint (palang_lsp_document_get_n_diagnostics (lsp_document), ==, 1);

  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* ---- Go to definition --------------------------------------------------- */

typedef struct {
  gboolean  done;
  gboolean  ok;
  GFile    *file;
  guint     line;
  guint     column;
  GError   *error;
} DefinitionAnswer;

static void
on_definition_found (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  DefinitionAnswer *answer = user_data;

  answer->ok = palang_lsp_manager_find_definition_finish (PALANG_LSP_MANAGER (source), result,
                                                          &answer->file, &answer->line,
                                                          &answer->column, &answer->error);
  answer->done = TRUE;
}

/* Asks for the definition at @line, @offset (GTK counting) and waits. */
static void
find_definition (PalangLspManager *manager,
                 PalangDocument   *document,
                 int               line,
                 int               offset,
                 DefinitionAnswer *answer)
{
  GtkTextIter iter;

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, line, offset);
  palang_lsp_manager_find_definition_async (manager, document, &iter, NULL,
                                            on_definition_found, answer);
  wait_for_flag (&answer->done, "the definition");
}

static void
clear_answer (DefinitionAnswer *answer)
{
  g_clear_object (&answer->file);
  g_clear_error (&answer->error);
}

/* The server answers with a Location (or LocationLinks, per @data): the
 * position comes back in characters. The target line starts with 😀, so
 * the server's character 3 is GTK's 2. */
static void
test_definition_found (Fixture *fixture, gconstpointer data)
{
  const char *option = data;
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, option);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c",
                                                       "\xf0\x9f\x98\x80 target\nuse target\n");
  g_autoptr (JsonNode) asked = NULL;
  DefinitionAnswer answer = { 0 };

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  find_definition (manager, document, 1, 5, &answer);
  g_assert_no_error (answer.error);
  g_assert_true (answer.ok);
  g_assert_nonnull (answer.file);
  g_assert_true (g_file_equal (answer.file, palang_document_get_file (document)));
  g_assert_cmpuint (answer.line, ==, 0);
  g_assert_cmpuint (answer.column, ==, 2);

  asked = get_logged_params (fixture, "textDocument/definition", 0);
  g_assert_cmpint (get_int (asked, "$.position.line"), ==, 1);
  g_assert_cmpint (get_int (asked, "$.position.character"), ==, 5);

  clear_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* The server found nothing (null): no file, no error. */
static void
test_definition_nothing (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", "int a;\n");
  DefinitionAnswer answer = { 0 };

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  find_definition (manager, document, 0, 4, &answer);
  g_assert_no_error (answer.error);
  g_assert_true (answer.ok);
  g_assert_null (answer.file);

  clear_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* The text changes before the (slow) answer comes: it is dropped. */
static void
test_definition_stale (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path,
                                                      "--definition 0:0 --slow");
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", "int a;\n");
  DefinitionAnswer answer = { 0 };
  GtkTextIter iter;

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, 0, 4);
  palang_lsp_manager_find_definition_async (manager, document, &iter, NULL,
                                            on_definition_found, &answer);
  insert_at (document, 0, 0, "x");  /* before the answer arrives */
  wait_for_flag (&answer.done, "the definition");

  g_assert_false (answer.ok);
  g_assert_error (answer.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  clear_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* A document no server handles: a clear error right away. */
static void
test_definition_no_server (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  g_autoptr (PalangLspManager) manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = new_document ("markdown");
  DefinitionAnswer answer = { 0 };

  palang_lsp_manager_add_document (manager, document);
  find_definition (manager, document, 0, 0, &answer);
  g_assert_false (answer.ok);
  g_assert_error (answer.error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED);

  clear_answer (&answer);
}

/* ---- Markdown ------------------------------------------------------------ */

/* Converts @markdown and checks the result is @expected and valid markup
 * (GtkLabel would show nothing if it weren't). */
static void
assert_markup (const char *markdown,
               const char *expected)
{
  g_autofree char *markup = palang_lsp_markdown_to_markup (markdown);
  g_autoptr (GError) error = NULL;

  g_assert_cmpstr (markup, ==, expected);
  g_assert_true (pango_parse_markup (markup, -1, 0, NULL, NULL, NULL, &error));
  g_assert_no_error (error);
}

/* Shaped like the real servers' answers (captured from clangd, pylsp and
 * typescript-language-server, shortened). */
static void
test_markdown_real_servers (void)
{
  /* clangd: heading, trailing "  " line breaks, separators, code block. */
  assert_markup ("### function `f`  \nprovided by `\"a.h\"`  \n\n---\n"
                 "→ `int`  \nAdds.  \n\n---\n```cpp\nint f(int *p)\n```",
                 "<b>function <tt>f</tt></b>\nprovided by <tt>&quot;a.h&quot;</tt>\n\n"
                 "→ <tt>int</tt>\nAdds.\n\n<tt>int f(int *p)</tt>");

  /* pylsp: a signature, empty lines, escaped stars. */
  assert_markup ("```python\narea(width: int) -> int\n```\n\n\nReturn the area.\n\n"
                 "Uses \\*\\*width\\*\\* times `height`.",
                 "<tt>area(width: int) -&gt; int</tt>\n\nReturn the area.\n\n"
                 "Uses **width** times <tt>height</tt>.");

  /* typescript-language-server: a leading empty line, italic tags. */
  assert_markup ("\n```typescript\nfunction add(a: any): any\n```\nAdds `a`.\n\n"
                 "*@param* `a` — first.  ",
                 "<tt>function add(a: any): any</tt>\nAdds <tt>a</tt>.\n\n"
                 "<i>@param</i> <tt>a</tt> — first.");
}

/* Text that only looks special stays as written, and is always valid
 * markup. */
static void
test_markdown_edge_cases (void)
{
  g_autofree char *escaped = palang_lsp_markdown_escape ("# a*b <c>\n---");

  assert_markup ("a * b * c", "a * b * c");
  assert_markup ("2*3 and *unclosed", "2*3 and *unclosed");
  assert_markup ("**bold `code*`**", "<b>bold <tt>code*</tt></b>");
  assert_markup ("vector<int> & x", "vector&lt;int&gt; &amp; x");
  assert_markup ("#include <x>", "#include &lt;x&gt;");
  assert_markup ("`never closed", "`never closed");
  assert_markup ("```\ncode <x>", "<tt>code &lt;x&gt;</tt>");  /* fence never closed */
  assert_markup ("", "");

  /* Plain text from a server, escaped, shows exactly as it was. */
  assert_markup (escaped, "# a*b &lt;c&gt;\n---");
}

/* ---- Hover --------------------------------------------------------------- */

typedef struct {
  gboolean  done;
  char     *text;   /* the answer, as Markdown */
  GError   *error;
} HoverAnswer;

static void
on_hover_answered (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  HoverAnswer *answer = user_data;

  answer->text = palang_lsp_manager_hover_finish (PALANG_LSP_MANAGER (source), result,
                                                  &answer->error);
  answer->done = TRUE;
}

/* The file the hover tests use: its line starts with 😀, so GTK's
 * character 2 is the server's 3. */
#define HOVER_TEXT "\xf0\x9f\x98\x80 x\n"

/* Asks for hover at @line, @offset (GTK counting) and waits. */
static void
hover_at (PalangLspManager *manager,
          PalangDocument   *document,
          int               line,
          int               offset,
          HoverAnswer      *answer)
{
  GtkTextIter iter;

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, line, offset);
  palang_lsp_manager_hover_async (manager, document, &iter, NULL, on_hover_answered, answer);
  wait_for_flag (&answer->done, "the hover answer");
}

static void
clear_hover_answer (HoverAnswer *answer)
{
  g_clear_pointer (&answer->text, g_free);
  g_clear_error (&answer->error);
}

/* The fake server answers in the form @data names ("--hover markdown",
 * …); the answer comes back as Markdown, about the right position. */
static void
test_hover_forms (Fixture *fixture, gconstpointer data)
{
  const char *option = data;
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, option);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", HOVER_TEXT);
  g_autofree char *markup = NULL;
  HoverAnswer answer = { 0 };

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  hover_at (manager, document, 0, 2, &answer);
  g_assert_no_error (answer.error);
  g_assert_nonnull (answer.text);

  if (strstr (option, "markdown") != NULL)
    g_assert_cmpstr (answer.text, ==, "**hover** at 0:3");
  else if (strstr (option, "plaintext") != NULL)
    {
      /* Plain text is escaped, so its stars stay stars. */
      markup = palang_lsp_markdown_to_markup (answer.text);
      g_assert_cmpstr (markup, ==, "**not bold** at 0:3");
    }
  else
    g_assert_cmpstr (answer.text, ==, "```c\nint x\n```\n\nplain *doc*");

  clear_hover_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* "null": nothing to say there. No text, no error. */
static void
test_hover_nothing (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, "--hover none");
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", HOVER_TEXT);
  HoverAnswer answer = { 0 };

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  hover_at (manager, document, 0, 2, &answer);
  g_assert_no_error (answer.error);
  g_assert_null (answer.text);

  clear_hover_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* A server that doesn't announce "hoverProvider" isn't asked. */
static void
test_hover_not_supported (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", HOVER_TEXT);
  g_autofree char *log = NULL;
  HoverAnswer answer = { 0 };

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  hover_at (manager, document, 0, 2, &answer);
  g_assert_error (answer.error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_null (answer.text);

  log = read_log (fixture);
  g_assert_null (strstr (log, "textDocument/hover"));

  clear_hover_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* The text changes before the (slow) answer comes: it is dropped. */
static void
test_hover_stale (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path,
                                                      "--hover markdown --slow");
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", HOVER_TEXT);
  HoverAnswer answer = { 0 };
  GtkTextIter iter;

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, 0, 2);
  palang_lsp_manager_hover_async (manager, document, &iter, NULL, on_hover_answered, &answer);
  insert_at (document, 0, 0, "y");  /* before the answer arrives */
  wait_for_flag (&answer.done, "the hover answer");

  g_assert_error (answer.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_null (answer.text);

  clear_hover_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* ---- Completion ---------------------------------------------------------- */

/* Returns: (transfer full): an item made from @json (a CompletionItem). */
static PalangLspCompletionItem *
item_from_json (const char *json)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (JsonNode) node = json_from_string (json, &error);
  g_autoptr (GVariant) variant = NULL;

  g_assert_no_error (error);
  /* jsonrpc-glib turns JSON into GVariants this way too. */
  variant = g_variant_ref_sink (json_gvariant_deserialize (node, NULL, &error));
  g_assert_no_error (error);
  return palang_lsp_completion_item_new (variant);
}

/* What an item shows and inserts, from the forms clangd and
 * typescript-language-server send. */
static void
test_completion_item_fields (void)
{
  g_autoptr (PalangLspCompletionItem) clangd = item_from_json (
    "{\"label\": \" alpha\", \"filterText\": \"alpha\", \"kind\": 5, \"detail\": \"int\","
    " \"sortText\": \"407d8856alpha\","
    " \"textEdit\": {\"newText\": \"alpha\", \"range\": {\"start\": {\"line\": 5, \"character\": 4},"
    "                                              \"end\": {\"line\": 5, \"character\": 6}}}}");
  g_autoptr (PalangLspCompletionItem) typescript = item_from_json (
    "{\"label\": \"map\", \"kind\": 2, \"sortText\": \"11\"}");
  g_autoptr (PalangLspCompletionItem) no_label = item_from_json ("{\"kind\": 2}");

  g_assert_cmpstr (palang_lsp_completion_item_get_label (clangd), ==, "alpha");
  g_assert_cmpstr (palang_lsp_completion_item_get_filter_text (clangd), ==, "alpha");
  g_assert_cmpstr (palang_lsp_completion_item_get_detail (clangd), ==, "int");
  g_assert_cmpstr (palang_lsp_completion_item_get_kind_name (clangd), ==, "Field");
  g_assert_cmpstr (palang_lsp_completion_item_get_new_text (clangd), ==, "alpha");

  /* Only a label: it is also what is matched and inserted. */
  g_assert_cmpstr (palang_lsp_completion_item_get_filter_text (typescript), ==, "map");
  g_assert_cmpstr (palang_lsp_completion_item_get_new_text (typescript), ==, "map");
  g_assert_cmpstr (palang_lsp_completion_item_get_sort_text (typescript), ==, "11");
  g_assert_cmpstr (palang_lsp_completion_item_get_kind_name (typescript), ==, "Method");
  g_assert_null (palang_lsp_completion_item_get_detail (typescript));

  g_assert_null (no_label);
}

/* Typed letters match in order; closer matches sort first; with nothing
 * typed, the server's sort text decides. */
static void
test_completion_item_matching (void)
{
  g_autoptr (PalangLspCompletionItem) long_name = item_from_json (
    "{\"label\": \"gtk_widget_show\", \"sortText\": \"1\"}");
  g_autoptr (PalangLspCompletionItem) short_name = item_from_json (
    "{\"label\": \"gwshow\", \"sortText\": \"2\"}");
  g_autoptr (PalangLspCompletionItem) other = item_from_json (
    "{\"label\": \"show\", \"sortText\": \"0\"}");
  g_autoptr (PalangLspCompletionItem) camel_case = item_from_json (
    "{\"label\": \"GtkWidget\"}");

  g_assert_true (palang_lsp_completion_item_match (long_name, "gws"));
  g_assert_true (palang_lsp_completion_item_match (short_name, "gws"));
  g_assert_false (palang_lsp_completion_item_match (other, "gws"));
  g_assert_cmpuint (palang_lsp_completion_item_get_score (other), ==, G_MAXUINT);
  g_assert_cmpint (palang_lsp_completion_item_compare (short_name, long_name), <, 0);

  /* Case is ignored in the item (the caller casefolds what is typed). */
  g_assert_true (palang_lsp_completion_item_match (camel_case, "gtkw"));

  g_assert_true (palang_lsp_completion_item_match (long_name, ""));
  g_assert_true (palang_lsp_completion_item_match (short_name, ""));
  g_assert_cmpint (palang_lsp_completion_item_compare (long_name, short_name), <, 0);
}

/* Inserting replaces the typed word (from the server's start, if given),
 * makes the extra edits, and is undone by one undo. */
static void
test_completion_item_insert (void)
{
  g_autoptr (PalangLspCompletionItem) with_edits = item_from_json (
    "{\"label\": \"alpha\","
    " \"textEdit\": {\"newText\": \"alpha\", \"range\": {\"start\": {\"line\": 1, \"character\": 2},"
    "                                              \"end\": {\"line\": 1, \"character\": 4}}},"
    " \"additionalTextEdits\": [{\"newText\": \"#include <a.h>\\n\","
    "                            \"range\": {\"start\": {\"line\": 0, \"character\": 0},"
    "                                      \"end\": {\"line\": 0, \"character\": 0}}}]}");
  g_autoptr (PalangLspCompletionItem) plain = item_from_json ("{\"label\": \"map\"}");
  g_autoptr (GtkSourceView) view = g_object_ref_sink (GTK_SOURCE_VIEW (gtk_source_view_new ()));
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  g_autofree char *text = NULL;
  GtkTextIter word_start, cursor, start, end;

  /* "s.al", with the word starting at "l" here and at "a" for the server:
   * the server's start wins. */
  gtk_text_buffer_set_text (buffer, "int x;\ns.al", -1);
  gtk_text_buffer_get_iter_at_line_offset (buffer, &word_start, 1, 3);
  gtk_text_buffer_get_end_iter (buffer, &cursor);
  palang_lsp_completion_item_insert (with_edits, view, &word_start, &cursor);

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  g_assert_cmpstr (text, ==, "#include <a.h>\nint x;\ns.alpha");
  g_clear_pointer (&text, g_free);

  gtk_text_buffer_undo (buffer);
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  g_assert_cmpstr (text, ==, "int x;\ns.al");
  g_clear_pointer (&text, g_free);

  /* No textEdit: the word ("ma") is replaced. */
  gtk_text_buffer_set_text (buffer, "xs.ma", -1);
  gtk_text_buffer_get_iter_at_offset (buffer, &word_start, 3);
  gtk_text_buffer_get_end_iter (buffer, &cursor);
  palang_lsp_completion_item_insert (plain, view, &word_start, &cursor);

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  g_assert_cmpstr (text, ==, "xs.map");
}

/* ---- Snippets ------------------------------------------------------------ */

/* Returns: (transfer full): @snippet's parts as "[stop:text]…", stop -1
 * for plain text. */
static char *
describe_snippet (const char *snippet)
{
  g_autoptr (GPtrArray) parts = palang_lsp_snippet_parse (snippet);
  GString *description = g_string_new (NULL);

  for (guint i = 0; i < parts->len; i++)
    {
      PalangLspSnippetPart *part = g_ptr_array_index (parts, i);

      g_string_append_printf (description, "[%d:%s]", part->tab_stop, part->text);
    }

  return g_string_free (description, FALSE);
}

static void
assert_snippet (const char *snippet,
                const char *expected)
{
  g_autofree char *description = describe_snippet (snippet);

  g_assert_cmpstr (description, ==, expected);
}

/* Every form of LSP's snippet syntax, and text that only looks like it. */
static void
test_snippet_parse (void)
{
  g_autofree char *text = NULL;

  assert_snippet ("printf(${1:const char *format}, $2)$0",
                  "[-1:printf(][1:const char *format][-1:, ][2:][-1:)][0:]");
  assert_snippet ("f(${1})", "[-1:f(][1:][-1:)]");
  assert_snippet ("${1|one,two|} \\$5 \\} a\\b", "[1:one][-1: $5 } a\\b]");
  assert_snippet ("${1|a\\,b,c|}", "[1:a,b]");                /* an escaped comma */
  assert_snippet ("x${1:a${2:nested}}y", "[-1:x][1:anested][-1:y]");
  assert_snippet ("$TM_FILENAME ${NAME:def}.", "[-1: def.]");  /* variables */
  assert_snippet ("a $ b }", "[-1:a $ b }]");
  assert_snippet ("${1:never closed", "[-1:${1:never closed]");
  assert_snippet ("${unclosed", "[-1:${unclosed]");
  assert_snippet ("", "");

  text = palang_lsp_snippet_to_text ("printf(${1:fmt}, $2)$0");
  g_assert_cmpstr (text, ==, "printf(fmt, )");
}

/* A snippet entry goes in as a snippet: its first place to fill in is
 * selected; one undo takes it all back. */
static void
test_completion_item_snippet (void)
{
  g_autoptr (PalangLspCompletionItem) item = item_from_json (
    "{\"label\": \"printf\", \"insertTextFormat\": 2,"
    " \"insertText\": \"printf(${1:fmt}, ${2:args})\"}");
  g_autoptr (GtkSourceView) view = g_object_ref_sink (GTK_SOURCE_VIEW (gtk_source_view_new ()));
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  g_autofree char *text = NULL;
  g_autofree char *selected = NULL;
  GtkTextIter word_start, cursor, start, end;

  g_assert_cmpstr (palang_lsp_completion_item_get_new_text (item), ==, "printf(fmt, args)");

  gtk_text_buffer_set_text (buffer, "x = pri;", -1);
  gtk_text_buffer_get_iter_at_offset (buffer, &word_start, 4);
  gtk_text_buffer_get_iter_at_offset (buffer, &cursor, 7);
  palang_lsp_completion_item_insert (item, view, &word_start, &cursor);

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  g_assert_cmpstr (text, ==, "x = printf(fmt, args);");

  g_assert_true (gtk_text_buffer_get_selection_bounds (buffer, &start, &end));
  selected = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  g_assert_cmpstr (selected, ==, "fmt");

  g_clear_pointer (&text, g_free);
  gtk_text_buffer_undo (buffer);
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  g_assert_cmpstr (text, ==, "x = pri;");
}

/* Documentation in both forms; plain text shows as it is. */
static void
test_completion_item_documentation (void)
{
  g_autoptr (PalangLspCompletionItem) markdown = item_from_json (
    "{\"label\": \"a\", \"documentation\": {\"kind\": \"markdown\", \"value\": \"**bold**\"}}");
  g_autoptr (PalangLspCompletionItem) plain = item_from_json (
    "{\"label\": \"b\", \"documentation\": \"**not bold**\"}");
  g_autoptr (PalangLspCompletionItem) none = item_from_json ("{\"label\": \"c\"}");
  g_autofree char *markup = NULL;

  g_assert_cmpstr (palang_lsp_completion_item_get_documentation (markdown), ==, "**bold**");
  markup = palang_lsp_markdown_to_markup (palang_lsp_completion_item_get_documentation (plain));
  g_assert_cmpstr (markup, ==, "**not bold**");
  g_assert_null (palang_lsp_completion_item_get_documentation (none));

  /* A resolved answer fills in what was missing. */
  g_assert_false (palang_lsp_completion_item_is_resolved (none));
  palang_lsp_completion_item_apply_resolved (none, NULL);
  g_assert_true (palang_lsp_completion_item_is_resolved (none));
  g_assert_null (palang_lsp_completion_item_get_documentation (none));
}

typedef struct {
  gboolean    done;
  GListModel *items;
  gboolean    incomplete;
  GError     *error;
} CompletionAnswer;

static void
on_completion_answered (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  CompletionAnswer *answer = user_data;

  answer->items = palang_lsp_manager_complete_finish (PALANG_LSP_MANAGER (source), result,
                                                      &answer->incomplete, &answer->error);
  answer->done = TRUE;
}

/* Asks for completion at @line, @offset (GTK counting) and waits. */
static void
complete_at (PalangLspManager *manager,
             PalangDocument   *document,
             int               line,
             int               offset,
             gunichar          trigger,
             CompletionAnswer *answer)
{
  GtkTextIter iter;

  gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (document), &iter, line, offset);
  palang_lsp_manager_complete_async (manager, document, &iter, trigger, NULL,
                                     on_completion_answered, answer);
  wait_for_flag (&answer->done, "the completion answer");
}

static void
clear_completion_answer (CompletionAnswer *answer)
{
  g_clear_object (&answer->items);
  g_clear_error (&answer->error);
  answer->done = FALSE;
}

/* The request: position in UTF-16 units, why we ask ("context"); the
 * answer: the items in the server's order, and "isIncomplete". */
static void
test_completion_request (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, "--complete");
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c",
                                                       "\xf0\x9f\x98\x80 s.a\n");
  g_autoptr (JsonNode) typed = NULL;
  g_autoptr (JsonNode) triggered = NULL;
  g_autoptr (PalangLspCompletionItem) first = NULL;
  g_autofree char *trigger = NULL;
  CompletionAnswer answer = { 0 };

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  complete_at (manager, document, 0, 5, 0, &answer);
  g_assert_no_error (answer.error);
  g_assert_true (answer.incomplete);
  g_assert_cmpuint (g_list_model_get_n_items (answer.items), ==, 2);
  first = g_list_model_get_item (answer.items, 0);
  g_assert_cmpstr (palang_lsp_completion_item_get_label (first), ==, "alpha");

  typed = get_logged_params (fixture, "textDocument/completion", 0);
  g_assert_cmpint (get_int (typed, "$.position.character"), ==, 6);  /* 😀 is two units */
  g_assert_cmpint (get_int (typed, "$.context.triggerKind"), ==, 1);

  clear_completion_answer (&answer);
  complete_at (manager, document, 0, 4, '.', &answer);
  g_assert_no_error (answer.error);
  triggered = get_logged_params (fixture, "textDocument/completion", 1);
  g_assert_cmpint (get_int (triggered, "$.context.triggerKind"), ==, 2);
  trigger = dup_string (triggered, "$.context.triggerCharacter");
  g_assert_cmpstr (trigger, ==, ".");

  clear_completion_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

typedef struct {
  gboolean  done;
  GVariant *item;
  GError   *error;
} ResolveAnswer;

static void
on_resolve_answered (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  ResolveAnswer *answer = user_data;

  answer->item = palang_lsp_manager_resolve_completion_finish (PALANG_LSP_MANAGER (source), result,
                                                               &answer->error);
  answer->done = TRUE;
}

/* "completionItem/resolve" sends the item as it came and brings back
 * documentation. */
static void
test_completion_resolve (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, "--complete");
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", "s.a\n");
  g_autoptr (PalangLspCompletionItem) first = NULL;
  g_autoptr (JsonNode) asked = NULL;
  g_autofree char *label = NULL;
  CompletionAnswer answer = { 0 };
  ResolveAnswer resolved = { 0 };

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  complete_at (manager, document, 0, 3, 0, &answer);
  g_assert_no_error (answer.error);
  first = g_list_model_get_item (answer.items, 0);
  g_assert_null (palang_lsp_completion_item_get_documentation (first));

  palang_lsp_manager_resolve_completion_async (manager, document,
                                               palang_lsp_completion_item_get_raw (first), NULL,
                                               on_resolve_answered, &resolved);
  wait_for_flag (&resolved.done, "the resolved item");
  g_assert_no_error (resolved.error);

  palang_lsp_completion_item_apply_resolved (first, resolved.item);
  g_assert_cmpstr (palang_lsp_completion_item_get_documentation (first), ==, "**resolved**  alpha");

  /* The server got the item exactly as it sent it (label with the space). */
  asked = get_logged_params (fixture, "completionItem/resolve", 0);
  label = dup_string (asked, "$.label");
  g_assert_cmpstr (label, ==, " alpha");

  g_clear_pointer (&resolved.item, g_variant_unref);
  clear_completion_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

/* A server without "completionProvider" isn't asked. */
static void
test_completion_not_supported (Fixture *fixture, gconstpointer data)
{
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path, NULL);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) document = load_new_file (fixture, "main.c", "s.a\n");
  g_autofree char *log = NULL;
  CompletionAnswer answer = { 0 };

  palang_lsp_manager_add_document (manager, document);
  wait_for_log (fixture, "textDocument/didOpen");

  complete_at (manager, document, 0, 3, 0, &answer);
  g_assert_error (answer.error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_null (answer.items);

  log = read_log (fixture);
  g_assert_null (strstr (log, "textDocument/completion"));

  clear_completion_answer (&answer);
  dispose_manager_and_wait (fixture, manager, palang_lsp_manager_get_client (manager, document));
}

int
main (int   argc,
      char *argv[])
{
  /* gtk_test_init (): g_test_init () plus GTK, for the views the
   * completion tests insert into. */
  gtk_test_init (&argc, &argv, NULL);
  gtk_source_init ();

  g_test_add_func ("/lsp/languages", test_languages);
  g_test_add_func ("/lsp/position/utf16", test_position_utf16);
  g_test_add_func ("/lsp/position/iters", test_position_iters);
  g_test_add_func ("/lsp/markdown/real-servers", test_markdown_real_servers);
  g_test_add_func ("/lsp/markdown/edge-cases", test_markdown_edge_cases);
  g_test_add_func ("/lsp/completion/item-fields", test_completion_item_fields);
  g_test_add_func ("/lsp/completion/item-matching", test_completion_item_matching);
  g_test_add_func ("/lsp/completion/item-insert", test_completion_item_insert);
  g_test_add_func ("/lsp/completion/item-snippet", test_completion_item_snippet);
  g_test_add_func ("/lsp/completion/item-documentation", test_completion_item_documentation);
  g_test_add_func ("/lsp/snippet/parse", test_snippet_parse);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD_TEST ("/lsp/client/start-and-stop", test_client_start_and_stop);
  ADD_TEST ("/lsp/client/no-folder", test_client_no_folder);
  ADD_TEST ("/lsp/client/call", test_client_call);
  ADD_TEST ("/lsp/client/path-includes-server-folder", test_client_path_includes_server_folder);
  ADD_TEST ("/lsp/client/missing-program", test_client_missing_program);
  ADD_TEST ("/lsp/client/restarts-then-gives-up", test_client_restarts_then_gives_up);
  ADD_TEST ("/lsp/client/initialize-refused", test_client_initialize_refused);
  ADD_TEST ("/lsp/client/kills-hanging-server", test_client_kills_hanging_server);
  ADD_TEST ("/lsp/manager/one-server-per-language", test_manager_one_server_per_language);
  ADD_TEST ("/lsp/manager/restarts-on-changes", test_manager_restarts_on_changes);
  ADD_TEST ("/lsp/manager/reports-missing-server-once", test_manager_reports_missing_server_once);
  ADD_TEST ("/lsp/sync/open-edit-close", test_sync_open_edit_close);
  ADD_TEST ("/lsp/sync/full-text", test_sync_full_text);
  ADD_TEST ("/lsp/sync/untitled-and-save-as", test_sync_untitled_and_save_as);
  ADD_TEST ("/lsp/sync/reopens-after-restart", test_sync_reopens_after_restart);
  ADD_TEST ("/lsp/diagnostics/shown-and-updated", test_diagnostics_shown_and_updated);
  ADD_TEST ("/lsp/diagnostics/stale-dropped", test_diagnostics_stale_dropped);
  g_test_add ("/lsp/definition/location", Fixture, "--definition 0:3",
              fixture_setup, test_definition_found, fixture_teardown);
  g_test_add ("/lsp/definition/location-link", Fixture, "--definition 0:3 --definition-link",
              fixture_setup, test_definition_found, fixture_teardown);
  ADD_TEST ("/lsp/definition/nothing", test_definition_nothing);
  ADD_TEST ("/lsp/definition/stale", test_definition_stale);
  ADD_TEST ("/lsp/definition/no-server", test_definition_no_server);
  g_test_add ("/lsp/hover/markdown", Fixture, "--hover markdown",
              fixture_setup, test_hover_forms, fixture_teardown);
  g_test_add ("/lsp/hover/plaintext", Fixture, "--hover plaintext",
              fixture_setup, test_hover_forms, fixture_teardown);
  g_test_add ("/lsp/hover/legacy", Fixture, "--hover legacy",
              fixture_setup, test_hover_forms, fixture_teardown);
  ADD_TEST ("/lsp/hover/nothing", test_hover_nothing);
  ADD_TEST ("/lsp/hover/not-supported", test_hover_not_supported);
  ADD_TEST ("/lsp/hover/stale", test_hover_stale);
  ADD_TEST ("/lsp/completion/request", test_completion_request);
  ADD_TEST ("/lsp/completion/resolve", test_completion_resolve);
  ADD_TEST ("/lsp/completion/not-supported", test_completion_not_supported);

#undef ADD_TEST

  return g_test_run ();
}
