/* test-lsp.c — tests for the language server client and manager.
 *
 * The server is tests/fake-lsp-server (its path comes from meson as
 * FAKE_LSP_SERVER), which logs every message it receives to a file; the
 * tests read that log to see what Palang sent.
 */

#include "config.h"

#include <glib/gstdio.h>
#include <jsonrpc-glib.h>
#include <string.h>

#include "palang-lsp-client.h"
#include "palang-lsp-languages.h"
#include "palang-lsp-manager.h"

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

/* Returns: (transfer full): settings in the fixture's folder, with
 * c-server set to the fake server (logging to @log_path) and the Python
 * server turned off. */
static PalangSettings *
new_settings (Fixture    *fixture,
              const char *log_path)
{
  g_autofree char *path = g_build_filename (fixture->dir, "config.ini", NULL);
  g_autofree char *quoted_server = g_shell_quote (FAKE_LSP_SERVER);
  g_autofree char *quoted_log = g_shell_quote (log_path);
  g_autofree char *command = g_strdup_printf ("%s --log %s", quoted_server, quoted_log);
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
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path);
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
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path);
  PalangLspManager *manager = palang_lsp_manager_new (settings);
  g_autoptr (PalangDocument) c_file = new_document ("c");
  g_autofree char *second_log = g_build_filename (fixture->dir, "second.log", NULL);
  g_autofree char *quoted_server = g_shell_quote (FAKE_LSP_SERVER);
  g_autofree char *quoted_log = g_shell_quote (second_log);
  g_autofree char *command = g_strdup_printf ("%s --log %s", quoted_server, quoted_log);
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
  g_autoptr (PalangSettings) settings = new_settings (fixture, fixture->log_path);
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

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  gtk_source_init ();

  g_test_add_func ("/lsp/languages", test_languages);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD_TEST ("/lsp/client/start-and-stop", test_client_start_and_stop);
  ADD_TEST ("/lsp/client/no-folder", test_client_no_folder);
  ADD_TEST ("/lsp/client/call", test_client_call);
  ADD_TEST ("/lsp/client/missing-program", test_client_missing_program);
  ADD_TEST ("/lsp/client/restarts-then-gives-up", test_client_restarts_then_gives_up);
  ADD_TEST ("/lsp/client/initialize-refused", test_client_initialize_refused);
  ADD_TEST ("/lsp/client/kills-hanging-server", test_client_kills_hanging_server);
  ADD_TEST ("/lsp/manager/one-server-per-language", test_manager_one_server_per_language);
  ADD_TEST ("/lsp/manager/restarts-on-changes", test_manager_restarts_on_changes);
  ADD_TEST ("/lsp/manager/reports-missing-server-once", test_manager_reports_missing_server_once);

#undef ADD_TEST

  return g_test_run ();
}
