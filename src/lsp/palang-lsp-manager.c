/* palang-lsp-manager.c — the language servers of one window.
 *
 * Two collections:
 *   clients    server key ("python") → PalangLspClient, for the servers
 *              that have been started (including failed ones, so a server
 *              that isn't installed is reported once, not for every file)
 *   documents  a PalangLspDocument for every open document: their
 *              languages decide which servers run, and each one keeps its
 *              server's copy of the text up to date
 *
 * Whenever something changes the set of servers (folder, settings,
 * restart), the affected documents are taken off their server (didClose),
 * the clients are stopped and removed, then ensure_client () runs for
 * every document: it starts what's missing and hands each document to its
 * server.
 */

#include "config.h"

#include <jsonrpc-glib.h>

#include "palang-lsp-diagnostics.h"
#include "palang-lsp-document.h"
#include "palang-lsp-hover-provider.h"
#include "palang-lsp-languages.h"
#include "palang-lsp-manager.h"
#include "palang-lsp-markdown.h"
#include "palang-lsp-position.h"

struct _PalangLspManager
{
  GObject parent_instance;

  PalangSettings *settings;  /* owned */
  GFile          *folder;    /* owned, nullable */

  /* Server key (static strings from palang-lsp-languages.c) → owned
   * PalangLspClient. */
  GHashTable *clients;

  GPtrArray *documents;  /* owned PalangLspDocument, one per open document */
};

G_DEFINE_FINAL_TYPE (PalangLspManager, palang_lsp_manager, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_FOLDER,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum {
  SIGNAL_SERVER_FAILED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Returns: (nullable): the server for @document's current language. */
static const PalangLspServerInfo *
find_server (PalangDocument *document)
{
  GtkSourceLanguage *language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (document));

  if (language == NULL)
    return NULL;

  return palang_lsp_find_server (gtk_source_language_get_id (language));
}

static void
on_client_failed (PalangLspClient  *client,
                  const char       *message,
                  PalangLspManager *self)
{
  g_signal_emit (self, signals[SIGNAL_SERVER_FAILED], 0, message);
}

/* A server's notification. "publishDiagnostics" goes to the document it
 * is about (by URI, among the documents on this server). */
static void
on_client_notification (PalangLspClient  *client,
                        const char       *method,
                        GVariant         *params,
                        PalangLspManager *self)
{
  const char *uri = NULL;
  gint64 version = -1;
  g_autoptr (GVariant) diagnostics = NULL;

  if (!g_str_equal (method, "textDocument/publishDiagnostics") ||
      params == NULL || !g_variant_is_of_type (params, G_VARIANT_TYPE_VARDICT) ||
      !g_variant_lookup (params, "uri", "&s", &uri))
    return;

  g_variant_lookup (params, "version", "x", &version);  /* optional */
  diagnostics = g_variant_lookup_value (params, "diagnostics", NULL);

  for (guint i = 0; i < self->documents->len; i++)
    {
      PalangLspDocument *lsp_document = g_ptr_array_index (self->documents, i);

      if (palang_lsp_document_get_client (lsp_document) == client &&
          g_strcmp0 (palang_lsp_document_get_uri (lsp_document), uri) == 0)
        palang_lsp_document_set_diagnostics (lsp_document, version, diagnostics);
    }
}

/* Returns: (transfer none) (nullable): the client for @server, started
 * now if it wasn't yet. NULL if the user turned this server off or its
 * command is invalid. */
static PalangLspClient *
get_or_start_client (PalangLspManager          *self,
                     const PalangLspServerInfo *server)
{
  g_autofree char *setting = NULL;
  g_autofree char *command = NULL;
  g_autofree char *default_command = NULL;
  g_auto (GStrv) argv = NULL;
  g_autoptr (GError) error = NULL;
  PalangLspClient *client = g_hash_table_lookup (self->clients, server->key);

  if (client != NULL)
    return client;

  setting = palang_lsp_dup_setting_name (server);
  command = palang_settings_dup_value (self->settings, setting);
  if (command == NULL || g_strstrip (command)[0] == '\0')
    return NULL;  /* the user turned this server off */

  /* "typescript-language-server --stdio" → its words, split the way a
   * shell would (so quotes work for paths with spaces). */
  if (!g_shell_parse_argv (command, NULL, &argv, &error))
    {
      g_autofree char *message =
        g_strdup_printf ("The setting “%s” isn't a valid command: %s", setting, error->message);

      g_signal_emit (self, signals[SIGNAL_SERVER_FAILED], 0, message);
      return NULL;
    }

  /* The install hint is only right for the server Palang suggests. */
  default_command = palang_settings_dup_default_value (setting);
  client = palang_lsp_client_new (server->title, (const char * const *) argv, self->folder,
                                  g_strcmp0 (command, default_command) == 0 ? server->install_hint
                                                                            : NULL);
  g_signal_connect (client, "failed", G_CALLBACK (on_client_failed), self);
  g_signal_connect (client, "notification", G_CALLBACK (on_client_notification), self);
  g_hash_table_insert (self->clients, (gpointer) server->key, client);

  palang_lsp_client_start (client);
  return client;
}

/* Hands @lsp_document to the server its language needs (starting it if
 * necessary), or to none. */
static void
ensure_client (PalangLspManager  *self,
               PalangLspDocument *lsp_document)
{
  const PalangLspServerInfo *server = find_server (palang_lsp_document_get_document (lsp_document));
  PalangLspClient *client = server != NULL ? get_or_start_client (self, server) : NULL;

  palang_lsp_document_set_client (lsp_document, client);
}

static void
ensure_all_clients (PalangLspManager *self)
{
  for (guint i = 0; i < self->documents->len; i++)
    ensure_client (self, g_ptr_array_index (self->documents, i));
}

/* Stops @client after taking its documents off it (didClose); the hash
 * table's reference is dropped by the caller. The client stays alive by
 * itself until its server has exited. */
static void
stop_client (PalangLspManager *self,
             PalangLspClient  *client)
{
  for (guint i = 0; i < self->documents->len; i++)
    {
      PalangLspDocument *lsp_document = g_ptr_array_index (self->documents, i);

      if (palang_lsp_document_get_client (lsp_document) == client)
        palang_lsp_document_set_client (lsp_document, NULL);
    }

  g_signal_handlers_disconnect_by_data (client, self);
  palang_lsp_client_stop (client);
}

static void
stop_all_clients (PalangLspManager *self)
{
  GHashTableIter iter;
  gpointer client;

  g_hash_table_iter_init (&iter, self->clients);
  while (g_hash_table_iter_next (&iter, NULL, &client))
    stop_client (self, client);
  g_hash_table_remove_all (self->clients);
}

void
palang_lsp_manager_restart (PalangLspManager *self)
{
  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));

  stop_all_clients (self);
  ensure_all_clients (self);
}

/* A setting changed. If it is a server's command, that server is started
 * again with the new command. */
static void
on_settings_notify (PalangSettings   *settings,
                    GParamSpec       *pspec,
                    PalangLspManager *self)
{
  GHashTableIter iter;
  gpointer key, client;

  g_hash_table_iter_init (&iter, self->clients);
  while (g_hash_table_iter_next (&iter, &key, &client))
    {
      g_autofree char *setting = g_strdup_printf ("%s-server", (const char *) key);

      if (g_str_equal (setting, pspec->name))
        {
          stop_client (self, client);
          g_hash_table_iter_remove (&iter);
        }
    }

  /* Also starts a server whose setting was empty until now. */
  ensure_all_clients (self);
}

/* ---- Documents ---------------------------------------------------------- */

/* Returns: (transfer none) (nullable): the LSP document for @document. */
static PalangLspDocument *
find_lsp_document (PalangLspManager *self,
                   PalangDocument   *document,
                   guint            *index)
{
  for (guint i = 0; i < self->documents->len; i++)
    {
      PalangLspDocument *lsp_document = g_ptr_array_index (self->documents, i);

      if (palang_lsp_document_get_document (lsp_document) == document)
        {
          if (index != NULL)
            *index = i;
          return lsp_document;
        }
    }

  return NULL;
}

/* E.g. Save As main.txt → main.py: now it needs pylsp. */
static void
on_document_language_changed (PalangDocument   *document,
                              GParamSpec       *pspec,
                              PalangLspManager *self)
{
  PalangLspDocument *lsp_document = find_lsp_document (self, document, NULL);

  if (lsp_document != NULL)
    ensure_client (self, lsp_document);
}

void
palang_lsp_manager_add_document (PalangLspManager *self,
                                 PalangDocument   *document)
{
  PalangLspDocument *lsp_document;

  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));
  g_return_if_fail (PALANG_IS_DOCUMENT (document));

  if (find_lsp_document (self, document, NULL) != NULL)
    return;

  lsp_document = palang_lsp_document_new (document);
  g_ptr_array_add (self->documents, lsp_document);
  g_signal_connect (document, "notify::language",
                    G_CALLBACK (on_document_language_changed), self);
  ensure_client (self, lsp_document);
}

void
palang_lsp_manager_remove_document (PalangLspManager *self,
                                    PalangDocument   *document)
{
  guint index;

  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));
  g_return_if_fail (PALANG_IS_DOCUMENT (document));

  if (find_lsp_document (self, document, &index) == NULL)
    return;

  g_signal_handlers_disconnect_by_data (document, self);
  /* The array's free function drops the LSP document, which sends
   * didClose. A server keeps running when its last document closes:
   * opening another file of that language is likely, and starting a
   * server can take seconds. */
  g_ptr_array_remove_index (self->documents, index);
}

PalangLspClient *
palang_lsp_manager_get_client (PalangLspManager *self,
                               PalangDocument   *document)
{
  const PalangLspServerInfo *server;

  g_return_val_if_fail (PALANG_IS_LSP_MANAGER (self), NULL);
  g_return_val_if_fail (PALANG_IS_DOCUMENT (document), NULL);

  server = find_server (document);
  if (server == NULL)
    return NULL;

  return g_hash_table_lookup (self->clients, server->key);
}

void
palang_lsp_manager_add_view (PalangLspManager *self,
                             GtkSourceView    *view)
{
  g_autoptr (PalangLspHoverProvider) provider = NULL;

  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));
  g_return_if_fail (GTK_SOURCE_IS_VIEW (view));

  palang_lsp_diagnostics_setup_view (view);

  /* The view's GtkSourceHover keeps its own reference to the provider. */
  provider = palang_lsp_hover_provider_new (self);
  gtk_source_hover_add_provider (gtk_source_view_get_hover (view),
                                 GTK_SOURCE_HOVER_PROVIDER (provider));
}

PalangLspDocument *
palang_lsp_manager_get_lsp_document (PalangLspManager *self,
                                     PalangDocument   *document)
{
  g_return_val_if_fail (PALANG_IS_LSP_MANAGER (self), NULL);
  g_return_val_if_fail (PALANG_IS_DOCUMENT (document), NULL);

  return find_lsp_document (self, document, NULL);
}

/* ---- Requests about a position ----------------------------------------- */

/* Go to definition and hover ask the server about one position in a
 * document. They share how the question is sent and how a stale answer
 * (the text changed meanwhile) is dropped. */

/* What a request remembers until the answer comes (the GTask's "task
 * data"). */
typedef struct {
  PalangLspDocument *lsp_document;  /* owned */
  gint64             version;       /* of the text the question was about */
} PositionRequest;

static void
position_request_free (PositionRequest *request)
{
  g_clear_object (&request->lsp_document);
  g_free (request);
}

/* Returns: TRUE if @client's server announced @capability (like
 * "hoverProvider") in its reply to "initialize". The value can be `true`
 * or an object of options; missing or `false` means no. */
static gboolean
client_supports (PalangLspClient *client,
                 const char      *capability)
{
  GVariant *capabilities = palang_lsp_client_get_capabilities (client);
  g_autoptr (GVariant) value = NULL;

  if (capabilities == NULL)
    return FALSE;

  value = g_variant_lookup_value (capabilities, capability, NULL);
  if (value == NULL)
    return FALSE;
  return !g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN) || g_variant_get_boolean (value);
}

/* Sends the request @method about the position @iter in @document;
 * @on_replied gets the reply with @task (transfer full) as its user data.
 * Fails @task at once if no server has the document open
 * (G_IO_ERROR_NOT_CONNECTED) or the server doesn't offer @capability
 * (G_IO_ERROR_NOT_SUPPORTED). */
static void
send_position_request (PalangLspManager    *self,
                       PalangDocument      *document,
                       const GtkTextIter   *iter,
                       const char          *method,
                       const char          *capability,
                       GTask               *task,
                       GAsyncReadyCallback  on_replied)
{
  g_autoptr (GVariant) params = NULL;
  PalangLspDocument *lsp_document = find_lsp_document (self, document, NULL);
  PalangLspClient *client = lsp_document != NULL ? palang_lsp_document_get_client (lsp_document) : NULL;
  PositionRequest *request;
  guint line, character;

  if (client == NULL || !palang_lsp_document_is_open (lsp_document))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                               "No language server has this file open");
      g_object_unref (task);
      return;
    }

  if (!client_supports (client, capability))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "The %s language server doesn't offer this",
                               palang_lsp_client_get_title (client));
      g_object_unref (task);
      return;
    }

  request = g_new0 (PositionRequest, 1);
  request->lsp_document = g_object_ref (lsp_document);
  request->version = palang_lsp_document_get_version (lsp_document);
  g_task_set_task_data (task, request, (GDestroyNotify) position_request_free);

  palang_lsp_position_from_iter (iter, &line, &character);
  params = JSONRPC_MESSAGE_NEW (
    "textDocument", "{",
      "uri", JSONRPC_MESSAGE_PUT_STRING (palang_lsp_document_get_uri (lsp_document)),
    "}",
    "position", "{",
      "line", JSONRPC_MESSAGE_PUT_INT64 (line),
      "character", JSONRPC_MESSAGE_PUT_INT64 (character),
    "}");
  palang_lsp_client_call_async (client, method, params, g_task_get_cancellable (task),
                                on_replied, task);
}

/* Returns: TRUE, after failing @task with G_IO_ERROR_CANCELLED, if the
 * text changed since the request was sent: the answer is about old
 * text. */
static gboolean
return_if_stale (GTask *task)
{
  PositionRequest *request = g_task_get_task_data (task);

  if (palang_lsp_document_get_version (request->lsp_document) == request->version)
    return FALSE;

  g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                           "The text changed while waiting for the server");
  return TRUE;
}

/* ---- Go to definition --------------------------------------------------- */

/* The result handed to _finish (). */
typedef struct {
  GFile *file;  /* owned, nullable */
  guint  line;
  guint  column;
} DefinitionResult;

static void
definition_result_free (DefinitionResult *result)
{
  g_clear_object (&result->file);
  g_free (result);
}

/* Reads one Location ({uri, range}) or LocationLink ({targetUri,
 * targetSelectionRange, …}) into @uri, @line and @character. */
static gboolean
parse_location (GVariant    *location,
                const char **uri,
                gint64      *line,
                gint64      *character)
{
  /* A LocationLink's "targetSelectionRange" is the name itself, its
   * "targetRange" the whole definition; the name is where to go. */
  if (JSONRPC_MESSAGE_PARSE (location,
        "uri", JSONRPC_MESSAGE_GET_STRING (uri),
        "range", "{",
          "start", "{",
            "line", JSONRPC_MESSAGE_GET_INT64 (line),
            "character", JSONRPC_MESSAGE_GET_INT64 (character),
          "}",
        "}"))
    return TRUE;

  return JSONRPC_MESSAGE_PARSE (location,
           "targetUri", JSONRPC_MESSAGE_GET_STRING (uri),
           "targetSelectionRange", "{",
             "start", "{",
               "line", JSONRPC_MESSAGE_GET_INT64 (line),
               "character", JSONRPC_MESSAGE_GET_INT64 (character),
             "}",
           "}");
}

/* Returns: (transfer none) (nullable): the open document showing @file. */
static PalangLspDocument *
find_lsp_document_for_file (PalangLspManager *self,
                            GFile            *file)
{
  for (guint i = 0; i < self->documents->len; i++)
    {
      PalangLspDocument *lsp_document = g_ptr_array_index (self->documents, i);
      GFile *document_file = palang_document_get_file (palang_lsp_document_get_document (lsp_document));

      if (document_file != NULL && g_file_equal (document_file, file))
        return lsp_document;
    }

  return NULL;
}

static void
on_definition_replied (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr (GTask) task = user_data;
  PalangLspManager *self = g_task_get_source_object (task);
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GVariant) location = NULL;
  g_autoptr (GError) error = NULL;
  DefinitionResult *found;
  PalangLspDocument *target;
  const char *uri = NULL;
  gint64 line = 0, character = 0;

  reply = palang_lsp_client_call_finish (PALANG_LSP_CLIENT (source), result, &error);
  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (return_if_stale (task))
    return;

  /* The answer is one Location, an array of them (we take the first), or
   * null (reply == NULL): nothing found. Arrays arrive as "av". */
  if (reply != NULL && g_variant_is_of_type (reply, G_VARIANT_TYPE_VARDICT))
    location = g_variant_ref (reply);
  else if (reply != NULL && g_variant_is_container (reply) && g_variant_n_children (reply) > 0)
    {
      location = g_variant_get_child_value (reply, 0);
      if (g_variant_is_of_type (location, G_VARIANT_TYPE_VARIANT))
        {
          GVariant *unboxed = g_variant_get_variant (location);

          g_variant_unref (location);
          location = unboxed;
        }
    }

  found = g_new0 (DefinitionResult, 1);
  if (location != NULL && g_variant_is_of_type (location, G_VARIANT_TYPE_VARDICT) &&
      parse_location (location, &uri, &line, &character))
    {
      found->file = g_file_new_for_uri (uri);
      found->line = (guint) MAX (line, 0);
      found->column = (guint) MAX (character, 0);

      /* If the file is open, convert the server's UTF-16 count into
       * characters exactly. */
      target = find_lsp_document_for_file (self, found->file);
      if (target != NULL)
        {
          GtkTextIter iter;

          palang_lsp_iter_from_position (GTK_TEXT_BUFFER (palang_lsp_document_get_document (target)),
                                         found->line, found->column, &iter);
          found->line = gtk_text_iter_get_line (&iter);
          found->column = gtk_text_iter_get_line_offset (&iter);
        }
    }

  g_task_return_pointer (task, found, (GDestroyNotify) definition_result_free);
}

void
palang_lsp_manager_find_definition_async (PalangLspManager    *self,
                                          PalangDocument      *document,
                                          const GtkTextIter   *iter,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));
  g_return_if_fail (PALANG_IS_DOCUMENT (document));
  g_return_if_fail (iter != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  send_position_request (self, document, iter, "textDocument/definition", "definitionProvider",
                         task, on_definition_replied);
}

gboolean
palang_lsp_manager_find_definition_finish (PalangLspManager  *self,
                                           GAsyncResult      *result,
                                           GFile            **file,
                                           guint             *line,
                                           guint             *column,
                                           GError           **error)
{
  DefinitionResult *found;

  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

  found = g_task_propagate_pointer (G_TASK (result), error);
  if (found == NULL)
    return FALSE;

  *file = g_steal_pointer (&found->file);
  *line = found->line;
  *column = found->column;
  definition_result_free (found);
  return TRUE;
}

/* ---- Hover -------------------------------------------------------------- */

/* Returns: (transfer full) (nullable): one piece of a hover answer as
 * Markdown. The protocol has three kinds:
 *   MarkupContent  {"kind": "markdown" or "plaintext", "value": …}
 *   MarkedString   "Markdown text", or {"language": "c", "value": code}
 * (MarkedString is the older form; some servers still send it.) */
static char *
hover_part_to_markdown (GVariant *part)
{
  const char *kind = NULL;
  const char *language = NULL;
  const char *value = NULL;

  if (g_variant_is_of_type (part, G_VARIANT_TYPE_STRING))
    return g_variant_dup_string (part, NULL);

  if (!g_variant_is_of_type (part, G_VARIANT_TYPE_VARDICT))
    return NULL;

  if (JSONRPC_MESSAGE_PARSE (part,
                             "kind", JSONRPC_MESSAGE_GET_STRING (&kind),
                             "value", JSONRPC_MESSAGE_GET_STRING (&value)))
    return g_str_equal (kind, "markdown") ? g_strdup (value) : palang_lsp_markdown_escape (value);

  if (JSONRPC_MESSAGE_PARSE (part,
                             "language", JSONRPC_MESSAGE_GET_STRING (&language),
                             "value", JSONRPC_MESSAGE_GET_STRING (&value)))
    return g_strdup_printf ("```%s\n%s\n```", language, value);

  return NULL;
}

/* Returns: (transfer full) (nullable): a hover answer's "contents" as
 * Markdown: one piece, or an array of them (arrives as "av") joined by
 * empty lines. NULL if there's no text. */
static char *
hover_contents_to_markdown (GVariant *contents)
{
  g_autoptr (GPtrArray) parts = g_ptr_array_new_with_free_func (g_free);
  char *markdown;

  if (!g_variant_is_of_type (contents, G_VARIANT_TYPE ("av")))
    {
      markdown = hover_part_to_markdown (contents);
    }
  else
    {
      for (gsize i = 0; i < g_variant_n_children (contents); i++)
        {
          g_autoptr (GVariant) boxed = g_variant_get_child_value (contents, i);
          g_autoptr (GVariant) part = g_variant_get_variant (boxed);
          char *text = hover_part_to_markdown (part);

          if (text != NULL)
            g_ptr_array_add (parts, text);
        }
      g_ptr_array_add (parts, NULL);  /* g_strjoinv () wants a NULL at the end */
      markdown = g_strjoinv ("\n\n", (char **) parts->pdata);
    }

  /* Servers sometimes answer with only spaces or empty lines. */
  if (markdown != NULL)
    {
      g_autofree char *stripped = g_strstrip (g_strdup (markdown));

      if (stripped[0] == '\0')
        g_clear_pointer (&markdown, g_free);
    }

  return markdown;
}

static void
on_hover_replied (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr (GTask) task = user_data;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GVariant) contents = NULL;
  g_autoptr (GError) error = NULL;

  reply = palang_lsp_client_call_finish (PALANG_LSP_CLIENT (source), result, &error);
  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (return_if_stale (task))
    return;

  /* A Hover is {"contents": …, "range": …}; null means nothing to say. */
  if (reply != NULL && g_variant_is_of_type (reply, G_VARIANT_TYPE_VARDICT))
    contents = g_variant_lookup_value (reply, "contents", NULL);

  g_task_return_pointer (task, contents != NULL ? hover_contents_to_markdown (contents) : NULL,
                         g_free);
}

void
palang_lsp_manager_hover_async (PalangLspManager    *self,
                                PalangDocument      *document,
                                const GtkTextIter   *iter,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));
  g_return_if_fail (PALANG_IS_DOCUMENT (document));
  g_return_if_fail (iter != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  send_position_request (self, document, iter, "textDocument/hover", "hoverProvider",
                         task, on_hover_replied);
}

char *
palang_lsp_manager_hover_finish (PalangLspManager  *self,
                                 GAsyncResult      *result,
                                 GError           **error)
{
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/* ---- Folder ------------------------------------------------------------- */

GFile *
palang_lsp_manager_get_folder (PalangLspManager *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_MANAGER (self), NULL);

  return self->folder;
}

/* A server is started for one folder, so a new folder means new servers. */
void
palang_lsp_manager_set_folder (PalangLspManager *self,
                               GFile            *folder)
{
  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));
  g_return_if_fail (folder == NULL || G_IS_FILE (folder));

  if (self->folder == folder ||
      (self->folder != NULL && folder != NULL && g_file_equal (self->folder, folder)))
    return;

  g_set_object (&self->folder, folder);
  palang_lsp_manager_restart (self);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLDER]);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_lsp_manager_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PalangLspManager *self = PALANG_LSP_MANAGER (object);

  switch (prop_id)
    {
    case PROP_FOLDER:
      g_value_set_object (value, self->folder);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_lsp_manager_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PalangLspManager *self = PALANG_LSP_MANAGER (object);

  switch (prop_id)
    {
    case PROP_FOLDER:
      palang_lsp_manager_set_folder (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_lsp_manager_dispose (GObject *object)
{
  PalangLspManager *self = PALANG_LSP_MANAGER (object);

  if (self->settings != NULL)
    g_signal_handlers_disconnect_by_data (self->settings, self);
  g_clear_object (&self->settings);

  /* Each server is asked to exit (after didClose for its documents); the
   * clients outlive us until it has. */
  if (self->clients != NULL)
    stop_all_clients (self);
  g_clear_pointer (&self->clients, g_hash_table_unref);

  if (self->documents != NULL)
    for (guint i = 0; i < self->documents->len; i++)
      {
        PalangLspDocument *lsp_document = g_ptr_array_index (self->documents, i);

        g_signal_handlers_disconnect_by_data (palang_lsp_document_get_document (lsp_document), self);
      }
  g_clear_pointer (&self->documents, g_ptr_array_unref);

  g_clear_object (&self->folder);

  G_OBJECT_CLASS (palang_lsp_manager_parent_class)->dispose (object);
}

static void
palang_lsp_manager_class_init (PalangLspManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = palang_lsp_manager_get_property;
  object_class->set_property = palang_lsp_manager_set_property;
  object_class->dispose = palang_lsp_manager_dispose;

  properties[PROP_FOLDER] =
    g_param_spec_object ("folder", NULL, NULL,
                         G_TYPE_FILE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[SIGNAL_SERVER_FAILED] =
    g_signal_new ("server-failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
palang_lsp_manager_init (PalangLspManager *self)
{
  /* Keys are static strings, so only the values (clients) are freed. */
  self->clients = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
  self->documents = g_ptr_array_new_with_free_func (g_object_unref);
}

PalangLspManager *
palang_lsp_manager_new (PalangSettings *settings)
{
  PalangLspManager *self;

  g_return_val_if_fail (PALANG_IS_SETTINGS (settings), NULL);

  self = g_object_new (PALANG_TYPE_LSP_MANAGER, NULL);
  self->settings = g_object_ref (settings);
  g_signal_connect (settings, "notify", G_CALLBACK (on_settings_notify), self);

  return self;
}
