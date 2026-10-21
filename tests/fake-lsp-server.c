/* fake-lsp-server.c — a tiny language server for the tests.
 *
 * It speaks JSON-RPC on stdin/stdout like a real server, but only knows
 * the protocol's start and end, plus one request of its own, so tests
 * don't depend on clangd or pylsp being installed.
 *
 *   fake-lsp-server [--log FILE] [--crash-after-initialized] [--ignore-shutdown]
 *                   [--refuse-initialize] [--full-sync] [--crash-on-change]
 *                   [--diagnose] [--definition LINE:CHARACTER]
 *                   [--definition-link] [--slow-definition]
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
 *   --diagnose                 after didOpen and (full-text) didChange,
 *                              publish an error for every "bad" and a
 *                              warning for every "meh" in the text; use
 *                              with --full-sync so it always has the text
 *   --definition LINE:CHAR     answer textDocument/definition with that
 *                              position in the same document (without it:
 *                              null, "nothing found")
 *   --definition-link          answer as an array of LocationLink instead
 *                              of one Location
 *   --slow-definition          answer it after 300 ms
 *
 * Requests it answers: "initialize", "shutdown", "textDocument/definition"
 * (see --definition), and "fake/echo", which replies with its params.
 */

#include "config.h"

#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <json-glib/json-glib.h>
#include <jsonrpc-glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char *log_path;
static gboolean crash_after_initialized;
static gboolean ignore_shutdown;
static gboolean refuse_initialize;
static gboolean full_sync;
static gboolean crash_on_change;
static gboolean diagnose;
static char *definition;  /* "LINE:CHAR" */
static gboolean definition_link;
static gboolean slow_definition;
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

/* Returns: (transfer full) (nullable): the answer to a definition request
 * about the document @uri, per --definition and --definition-link. */
static GVariant *
build_definition (const char *uri)
{
  gint64 line = 0, character = 0;

  if (definition == NULL || sscanf (definition, "%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT,
                                    &line, &character) != 2)
    return NULL;

  if (definition_link)
    return JSONRPC_MESSAGE_NEW_ARRAY (
      "{",
        "targetUri", JSONRPC_MESSAGE_PUT_STRING (uri),
        "targetRange", "{",
          "start", "{", "line", JSONRPC_MESSAGE_PUT_INT64 (0), "character", JSONRPC_MESSAGE_PUT_INT64 (0), "}",
          "end", "{", "line", JSONRPC_MESSAGE_PUT_INT64 (line + 1), "character", JSONRPC_MESSAGE_PUT_INT64 (0), "}",
        "}",
        "targetSelectionRange", "{",
          "start", "{", "line", JSONRPC_MESSAGE_PUT_INT64 (line), "character", JSONRPC_MESSAGE_PUT_INT64 (character), "}",
          "end", "{", "line", JSONRPC_MESSAGE_PUT_INT64 (line), "character", JSONRPC_MESSAGE_PUT_INT64 (character + 1), "}",
        "}",
      "}");

  return JSONRPC_MESSAGE_NEW (
    "uri", JSONRPC_MESSAGE_PUT_STRING (uri),
    "range", "{",
      "start", "{", "line", JSONRPC_MESSAGE_PUT_INT64 (line), "character", JSONRPC_MESSAGE_PUT_INT64 (character), "}",
      "end", "{", "line", JSONRPC_MESSAGE_PUT_INT64 (line), "character", JSONRPC_MESSAGE_PUT_INT64 (character + 1), "}",
    "}");
}

/* A reply sent later (--slow-definition). */
typedef struct {
  JsonrpcClient *rpc;    /* owned */
  GVariant      *id;     /* owned */
  GVariant      *reply;  /* owned, nullable */
} LateReply;

static gboolean
send_late_reply (gpointer user_data)
{
  LateReply *late = user_data;

  jsonrpc_client_reply_async (late->rpc, late->id, late->reply, NULL, NULL, NULL);
  g_object_unref (late->rpc);
  g_variant_unref (late->id);
  g_clear_pointer (&late->reply, g_variant_unref);
  g_free (late);
  return G_SOURCE_REMOVE;
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
  else if (g_str_equal (method, "textDocument/definition"))
    {
      const char *uri = NULL;
      g_autoptr (GVariant) reply = NULL;

      JSONRPC_MESSAGE_PARSE (params, "textDocument", "{", "uri", JSONRPC_MESSAGE_GET_STRING (&uri), "}");
      reply = build_definition (uri);

      if (slow_definition)
        {
          LateReply *late = g_new0 (LateReply, 1);

          late->rpc = g_object_ref (rpc);
          late->id = g_variant_ref (id);
          late->reply = g_steal_pointer (&reply);
          g_timeout_add (300, send_late_reply, late);
        }
      else
        jsonrpc_client_reply_async (rpc, id, reply, NULL, NULL, NULL);
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

/* Returns: the UTF-16 length of the first @n_bytes of @text, computed
 * with GLib's converter (not Palang's code, which is what is tested). */
static gint64
utf16_length (const char *text,
              gsize       n_bytes)
{
  glong n_units = 0;
  g_autofree gunichar2 *converted = g_utf8_to_utf16 (text, (glong) n_bytes, NULL, &n_units, NULL);

  return n_units;
}

/* Adds a diagnostic for every @word in @text to @builder. */
static void
find_word (GVariantBuilder *builder,
           const char      *text,
           const char      *word,
           gint64           severity)
{
  g_auto (GStrv) lines = g_strsplit (text, "\n", -1);

  for (guint line = 0; lines[line] != NULL; line++)
    for (const char *found = strstr (lines[line], word); found != NULL;
         found = strstr (found + 1, word))
      {
        gint64 start = utf16_length (lines[line], found - lines[line]);
        g_autoptr (GVariant) diagnostic = JSONRPC_MESSAGE_NEW (
          "range", "{",
            "start", "{",
              "line", JSONRPC_MESSAGE_PUT_INT64 (line),
              "character", JSONRPC_MESSAGE_PUT_INT64 (start),
            "}",
            "end", "{",
              "line", JSONRPC_MESSAGE_PUT_INT64 (line),
              "character", JSONRPC_MESSAGE_PUT_INT64 (start + utf16_length (word, strlen (word))),
            "}",
          "}",
          "severity", JSONRPC_MESSAGE_PUT_INT64 (severity),
          "source", JSONRPC_MESSAGE_PUT_STRING ("fake"),
          "message", JSONRPC_MESSAGE_PUT_STRING (word));

        g_variant_builder_add (builder, "v", diagnostic);
      }
}

/* textDocument/publishDiagnostics for @text, version @version of @uri. */
static void
publish_diagnostics (JsonrpcClient *rpc,
                     const char    *uri,
                     gint64         version,
                     const char    *text)
{
  GVariantBuilder builder;
  g_autoptr (GVariant) params = NULL;
  g_autoptr (GVariantDict) dict = NULL;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));
  find_word (&builder, text, "bad", 1);
  find_word (&builder, text, "meh", 2);

  params = JSONRPC_MESSAGE_NEW (
    "uri", JSONRPC_MESSAGE_PUT_STRING (uri),
    "version", JSONRPC_MESSAGE_PUT_INT64 (version));
  dict = g_variant_dict_new (params);
  g_variant_dict_insert_value (dict, "diagnostics", g_variant_builder_end (&builder));
  g_clear_pointer (&params, g_variant_unref);
  params = g_variant_ref_sink (g_variant_dict_end (dict));

  jsonrpc_client_send_notification_async (rpc, "textDocument/publishDiagnostics", params,
                                          NULL, NULL, NULL);
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

  if (diagnose && g_str_equal (method, "textDocument/didOpen"))
    {
      const char *uri = NULL, *text = NULL;
      gint64 version = 0;

      if (JSONRPC_MESSAGE_PARSE (params, "textDocument", "{",
                                   "uri", JSONRPC_MESSAGE_GET_STRING (&uri),
                                   "version", JSONRPC_MESSAGE_GET_INT64 (&version),
                                   "text", JSONRPC_MESSAGE_GET_STRING (&text),
                                 "}"))
        publish_diagnostics (rpc, uri, version, text);
    }

  /* With --full-sync, the one change is the whole new text. */
  if (diagnose && g_str_equal (method, "textDocument/didChange"))
    {
      const char *uri = NULL, *text = NULL;
      gint64 version = 0;
      g_autoptr (GVariant) changes = NULL;
      g_autoptr (GVariant) first = NULL;

      if (JSONRPC_MESSAGE_PARSE (params, "textDocument", "{",
                                   "uri", JSONRPC_MESSAGE_GET_STRING (&uri),
                                   "version", JSONRPC_MESSAGE_GET_INT64 (&version),
                                 "}") &&
          (changes = g_variant_lookup_value (params, "contentChanges", NULL)) != NULL &&
          g_variant_n_children (changes) > 0)
        {
          first = g_variant_get_child_value (changes, 0);
          if (g_variant_is_of_type (first, G_VARIANT_TYPE_VARIANT))
            {
              GVariant *unboxed = g_variant_get_variant (first);

              g_variant_unref (first);
              first = unboxed;
            }
          if (g_variant_lookup (first, "text", "&s", &text))
            publish_diagnostics (rpc, uri, version, text);
        }
    }

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
    { "diagnose", 0, 0, G_OPTION_ARG_NONE, &diagnose, NULL, NULL },
    { "definition", 0, 0, G_OPTION_ARG_STRING, &definition, NULL, NULL },
    { "definition-link", 0, 0, G_OPTION_ARG_NONE, &definition_link, NULL, NULL },
    { "slow-definition", 0, 0, G_OPTION_ARG_NONE, &slow_definition, NULL, NULL },
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
  g_free (definition);
  return 0;
}
