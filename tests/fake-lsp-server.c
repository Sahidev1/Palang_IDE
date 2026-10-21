/* fake-lsp-server.c — a tiny language server for the tests.
 *
 * It speaks JSON-RPC on stdin/stdout like a real server, but only knows
 * the protocol's start and end, plus one request of its own, so tests
 * don't depend on clangd or pylsp being installed.
 *
 *   fake-lsp-server [--log FILE] [--crash-after-initialized] [--ignore-shutdown]
 *                   [--refuse-initialize] [--full-sync] [--crash-on-change]
 *
 *   --log FILE                 append every message received to FILE, one
 *                              line each: the method, a space, the params
 *                              as JSON. Tests read it to see what Palang
 *                              sent.
 *   --crash-after-initialized  exit with status 1 as soon as "initialized"
 *                              arrives (a server that keeps crashing)
 *   --ignore-shutdown          never answer "shutdown" and don't leave on
 *                              "exit" (a server that hangs)
 *   --refuse-initialize        answer "initialize" with an error (a server
 *                              missing something it needs)
 *   --full-sync                ask for the whole text on every change
 *                              (default: only the edited range)
 *   --crash-on-change          exit with status 1 when a didChange arrives
 *
 * Requests it answers: "initialize", "shutdown", and "fake/echo", which
 * replies with its params.
 */

#include "config.h"

#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <json-glib/json-glib.h>
#include <jsonrpc-glib.h>
#include <stdio.h>
#include <unistd.h>

static char *log_path;
static gboolean crash_after_initialized;
static gboolean ignore_shutdown;
static gboolean refuse_initialize;
static gboolean full_sync;
static gboolean crash_on_change;
static GMainLoop *main_loop;

static void
log_message (const char *method,
             GVariant   *params)
{
  g_autofree char *json = NULL;
  FILE *file;

  if (log_path == NULL)
    return;

  if (params != NULL)
    {
      JsonNode *node = json_gvariant_serialize (params);

      json = json_to_string (node, FALSE);
      json_node_unref (node);
    }

  file = fopen (log_path, "a");
  if (file == NULL)
    return;
  fprintf (file, "%s %s\n", method, json != NULL ? json : "null");
  fclose (file);
}

static gboolean
on_handle_call (JsonrpcClient *rpc,
                const char    *method,
                GVariant      *id,
                GVariant      *params,
                gpointer       user_data)
{
  log_message (method, params);

  if (g_str_equal (method, "initialize") && refuse_initialize)
    {
      jsonrpc_client_reply_error_async (rpc, id, JSONRPC_CLIENT_ERROR_INTERNAL_ERROR,
                                        "Something it needs is missing", NULL, NULL, NULL);
    }
  else if (g_str_equal (method, "initialize"))
    {
      g_autoptr (GVariant) reply = JSONRPC_MESSAGE_NEW (
        "capabilities", "{",
          "textDocumentSync", JSONRPC_MESSAGE_PUT_INT64 (full_sync ? 1 : 2),
        "}",
        "serverInfo", "{",
          "name", JSONRPC_MESSAGE_PUT_STRING ("fake"),
        "}");

      jsonrpc_client_reply_async (rpc, id, reply, NULL, NULL, NULL);
    }
  else if (g_str_equal (method, "shutdown"))
    {
      if (!ignore_shutdown)
        jsonrpc_client_reply_async (rpc, id, NULL, NULL, NULL, NULL);
    }
  else if (g_str_equal (method, "fake/echo"))
    {
      jsonrpc_client_reply_async (rpc, id, params, NULL, NULL, NULL);
    }
  else
    {
      jsonrpc_client_reply_error_async (rpc, id, JSONRPC_CLIENT_ERROR_METHOD_NOT_FOUND,
                                        "Unknown method", NULL, NULL, NULL);
    }

  return TRUE;
}

static void
on_notification (JsonrpcClient *rpc,
                 const char    *method,
                 GVariant      *params,
                 gpointer       user_data)
{
  log_message (method, params);

  /* _exit skips the sanitizers' leak check: this is a pretend crash. */
  if (g_str_equal (method, "initialized") && crash_after_initialized)
    _exit (1);
  if (g_str_equal (method, "textDocument/didChange") && crash_on_change)
    _exit (1);

  if (g_str_equal (method, "exit") && !ignore_shutdown)
    g_main_loop_quit (main_loop);
}

/* stdin was closed: Palang is gone. */
static void
on_failed (JsonrpcClient *rpc,
           gpointer       user_data)
{
  g_main_loop_quit (main_loop);
}

int
main (int   argc,
      char *argv[])
{
  GOptionEntry entries[] = {
    { "log", 0, 0, G_OPTION_ARG_FILENAME, &log_path, NULL, NULL },
    { "crash-after-initialized", 0, 0, G_OPTION_ARG_NONE, &crash_after_initialized, NULL, NULL },
    { "ignore-shutdown", 0, 0, G_OPTION_ARG_NONE, &ignore_shutdown, NULL, NULL },
    { "refuse-initialize", 0, 0, G_OPTION_ARG_NONE, &refuse_initialize, NULL, NULL },
    { "full-sync", 0, 0, G_OPTION_ARG_NONE, &full_sync, NULL, NULL },
    { "crash-on-change", 0, 0, G_OPTION_ARG_NONE, &crash_on_change, NULL, NULL },
    { NULL }
  };
  g_autoptr (GOptionContext) context = g_option_context_new (NULL);
  g_autoptr (GError) error = NULL;
  g_autoptr (GInputStream) input = NULL;
  g_autoptr (GOutputStream) output = NULL;
  g_autoptr (GIOStream) stream = NULL;
  g_autoptr (JsonrpcClient) rpc = NULL;

  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return 2;
    }

  input = g_unix_input_stream_new (STDIN_FILENO, FALSE);
  output = g_unix_output_stream_new (STDOUT_FILENO, FALSE);
  stream = g_simple_io_stream_new (input, output);

  /* JsonrpcClient works in both directions, so it serves as the server
   * side of one connection too. */
  rpc = jsonrpc_client_new (stream);
  g_signal_connect (rpc, "handle-call", G_CALLBACK (on_handle_call), NULL);
  g_signal_connect (rpc, "notification", G_CALLBACK (on_notification), NULL);
  g_signal_connect (rpc, "failed", G_CALLBACK (on_failed), NULL);
  jsonrpc_client_start_listening (rpc);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);

  g_free (log_path);
  return 0;
}
