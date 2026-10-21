/* palang-lsp-client.c — one running language server.
 *
 * How a server's life goes (the `state` field):
 *
 *   STOPPED ──start──▶ STARTING ──"initialize" replied──▶ READY
 *                         │  ▲                              │
 *                         │  └──── crashed, restart ◀───────┤
 *                         ▼                                 ▼
 *                       FAILED  (not found, or         STOPPING ──exited──▶ STOPPED
 *                                crashed too often)    (stop was called)
 *
 * The server is a child process (GSubprocess) whose stdin and stdout are
 * the JSON-RPC connection (JsonrpcClient from jsonrpc-glib, which also
 * does the protocol's "Content-Length:" framing). Everything is
 * asynchronous; nothing here waits for the server.
 *
 * Each run of the server process gets its own GCancellable. Ending a run
 * (stop_process) cancels it, so callbacks still pending for that run see
 * G_IO_ERROR_CANCELLED and return without touching the client, which may
 * already be gone. That's why those callbacks don't hold a reference.
 *
 * The server's own messages on stderr are thrown away (clangd writes a
 * lot). Set PALANG_LSP_STDERR=1 to see them in Palang's terminal.
 */

#include "config.h"

#include <jsonrpc-glib.h>
#include <unistd.h>

#include "palang-lsp-client.h"

/* A server that crashes more than MAX_RESTARTS times within
 * RESTART_WINDOW_SECONDS isn't started again. */
#define MAX_RESTARTS            3
#define RESTART_WINDOW_SECONDS  60

/* How long a stopping server gets to exit before it is killed. */
#define STOP_TIMEOUT_SECONDS    2

typedef enum {
  STATE_STOPPED,
  STATE_STARTING,
  STATE_READY,
  STATE_STOPPING,
  STATE_FAILED,
} State;

struct _PalangLspClient
{
  GObject parent_instance;

  char   *title;         /* owned; "Python" */
  char  **argv;          /* owned */
  GFile  *root;          /* owned, nullable */
  char   *install_hint;  /* owned, nullable */

  State state;

  /* The current run of the server; all NULL while it isn't running. */
  GSubprocess   *process;       /* owned */
  JsonrpcClient *rpc;           /* owned */
  GCancellable  *cancellable;   /* owned; cancelled when the run ends */
  GVariant      *capabilities;  /* owned; set once READY */

  /* Crashes counted since first_crash_time (g_get_monotonic_time ()). */
  guint  n_crashes;
  gint64 first_crash_time;

  /* While STOPPING: the timeout that kills the server, and the application
   * we hold so Palang doesn't quit before the server is gone. */
  guint         stop_timeout_id;
  GApplication *held_application;  /* owned, nullable */
};

G_DEFINE_FINAL_TYPE (PalangLspClient, palang_lsp_client, G_TYPE_OBJECT)

enum {
  SIGNAL_READY,
  SIGNAL_FAILED,
  SIGNAL_NOTIFICATION,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void on_process_exited (GObject      *source,
                               GAsyncResult *result,
                               gpointer      user_data);

/* ---- Ending a run ------------------------------------------------------- */

/* Ends the current run of the server, if any: forgets the connection and
 * kills the process (if it hasn't exited already). */
static void
stop_process (PalangLspClient *self)
{
  if (self->cancellable != NULL)
    g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  if (self->rpc != NULL)
    {
      g_autoptr (GError) error = NULL;

      g_signal_handlers_disconnect_by_data (self->rpc, self);
      /* Closing a connection to a server that has already exited fails;
       * that is expected and not worth more than a debug message. */
      if (!jsonrpc_client_close (self->rpc, NULL, &error))
        g_debug ("Closing the connection to the %s language server: %s",
                 self->title, error->message);
      g_clear_object (&self->rpc);
    }

  if (self->process != NULL)
    {
      g_subprocess_force_exit (self->process);
      g_clear_object (&self->process);
    }

  g_clear_pointer (&self->capabilities, g_variant_unref);
}

static void
fail (PalangLspClient *self,
      const char      *message)
{
  stop_process (self);
  self->state = STATE_FAILED;
  g_signal_emit (self, signals[SIGNAL_FAILED], 0, message);
}

/* ---- Messages from the server ------------------------------------------- */

static void
on_rpc_notification (JsonrpcClient   *rpc,
                     const char      *method,
                     GVariant        *params,
                     PalangLspClient *self)
{
  g_signal_emit (self, signals[SIGNAL_NOTIFICATION], 0, method, params);
}

/* The server asks *us* something. Servers may send these requests even
 * though our "initialize" didn't offer them; a reply keeps them from
 * waiting. Returns TRUE because we always reply. */
static gboolean
on_rpc_handle_call (JsonrpcClient   *rpc,
                    const char      *method,
                    GVariant        *id,
                    GVariant        *params,
                    PalangLspClient *self)
{
  g_autoptr (GVariant) reply = NULL;

  if (g_str_equal (method, "workspace/configuration"))
    {
      /* "What are your settings for these sections?" One JSON null per
       * requested item means "no settings, use your defaults". */
      g_autoptr (GVariant) items = NULL;
      GVariantBuilder builder;

      g_variant_builder_init (&builder, G_VARIANT_TYPE ("amv"));
      if (g_variant_lookup (params, "items", "@av", &items))
        for (gsize i = 0; i < g_variant_n_children (items); i++)
          g_variant_builder_add (&builder, "mv", NULL);
      reply = g_variant_ref_sink (g_variant_builder_end (&builder));
    }
  else if (g_str_equal (method, "window/workDoneProgress/create") ||
           g_str_equal (method, "client/registerCapability") ||
           g_str_equal (method, "client/unregisterCapability"))
    {
      /* Fine with us; the reply is JSON null (reply stays NULL). */
    }
  else
    {
      jsonrpc_client_reply_error_async (rpc, id, JSONRPC_CLIENT_ERROR_METHOD_NOT_FOUND,
                                        "Not supported by Palang", NULL, NULL, NULL);
      return TRUE;
    }

  jsonrpc_client_reply_async (rpc, id, reply, NULL, NULL, NULL);
  return TRUE;
}

/* ---- Starting ----------------------------------------------------------- */

static void
on_initialized_sent (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  if (!jsonrpc_client_send_notification_finish (JSONRPC_CLIENT (source), result, &error))
    g_debug ("Sending “initialized”: %s", error->message);
}

static void
on_initialize_replied (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  PalangLspClient *self = user_data;  /* not a reference; see the top */
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GVariant) params = NULL;
  g_autoptr (GError) error = NULL;

  if (!jsonrpc_client_call_finish (JSONRPC_CLIENT (source), result, &reply, &error))
    {
      g_autofree char *message = NULL;

      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      /* The connection broke: the server crashed while starting.
       * on_process_exited decides whether to try again. */
      if (error->domain != JSONRPC_CLIENT_ERROR)
        return;

      /* The server answered with an error ("Could not find a valid
       * TypeScript installation"). Starting it again would give the same
       * answer, so give up and show the server's own explanation. */
      message = g_strdup_printf ("The %s language server “%s” couldn't start: %s",
                                 self->title, self->argv[0], error->message);
      fail (self, message);
      return;
    }

  if (reply != NULL && g_variant_is_of_type (reply, G_VARIANT_TYPE_VARDICT))
    self->capabilities = g_variant_lookup_value (reply, "capabilities", NULL);

  /* The handshake ends with our "initialized" notification. Its params are
   * an empty JSON object: an empty a{sv}. (g_variant_new returns a
   * "floating" reference; ref_sink makes it ours, for g_autoptr.) */
  params = g_variant_ref_sink (g_variant_new ("a{sv}", NULL));
  jsonrpc_client_send_notification_async (self->rpc, "initialized", params,
                                          self->cancellable, on_initialized_sent, NULL);

  self->state = STATE_READY;
  g_signal_emit (self, signals[SIGNAL_READY], 0);
}

/* The "initialize" request: who we are, the project folder, and what we
 * can handle ("client capabilities"). The capabilities list what Phase 2b
 * uses: document sync with save, diagnostics, go to definition. */
static void
send_initialize (PalangLspClient *self)
{
  g_autofree char *root_uri = NULL;
  g_autoptr (GVariant) params = NULL;

  if (self->root != NULL)
    root_uri = g_file_get_uri (self->root);

  /* With no folder open, root_uri is NULL, which becomes JSON null: the
   * protocol's way to say "no folder". */
  params = JSONRPC_MESSAGE_NEW (
    "processId", JSONRPC_MESSAGE_PUT_INT64 (getpid ()),
    "clientInfo", "{",
      "name", JSONRPC_MESSAGE_PUT_STRING ("Palang"),
      "version", JSONRPC_MESSAGE_PUT_STRING (PACKAGE_VERSION),
    "}",
    "rootUri", JSONRPC_MESSAGE_PUT_STRING (root_uri),
    "capabilities", "{",
      "general", "{",
        "positionEncodings", "[", JSONRPC_MESSAGE_PUT_STRING ("utf-16"), "]",
      "}",
      "workspace", "{",
        "workspaceFolders", JSONRPC_MESSAGE_PUT_BOOLEAN (TRUE),
      "}",
      "textDocument", "{",
        "synchronization", "{",
          "didSave", JSONRPC_MESSAGE_PUT_BOOLEAN (TRUE),
        "}",
        "publishDiagnostics", "{", "}",
        "definition", "{", "}",
      "}",
    "}");

  /* Newer servers read the folder from "workspaceFolders" instead. A
   * GVariantDict edits an a{sv} dictionary (a JSON object) by key. */
  if (root_uri != NULL)
    {
      g_autofree char *root_name = g_file_get_basename (self->root);
      g_autoptr (GVariantDict) dict = g_variant_dict_new (params);
      g_autoptr (GVariant) folders = JSONRPC_MESSAGE_NEW_ARRAY (
        "{",
          "uri", JSONRPC_MESSAGE_PUT_STRING (root_uri),
          "name", JSONRPC_MESSAGE_PUT_STRING (root_name),
        "}");

      g_variant_dict_insert_value (dict, "workspaceFolders", folders);
      g_clear_pointer (&params, g_variant_unref);
      params = g_variant_ref_sink (g_variant_dict_end (dict));
    }

  jsonrpc_client_call_async (self->rpc, "initialize", params, self->cancellable,
                             on_initialize_replied, self);
}

void
palang_lsp_client_start (PalangLspClient *self)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GIOStream) stream = NULL;
  g_autoptr (GError) error = NULL;
  GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE;

  g_return_if_fail (PALANG_IS_LSP_CLIENT (self));

  if (self->state == STATE_STARTING || self->state == STATE_READY ||
      self->state == STATE_STOPPING)
    return;

  if (g_strcmp0 (g_getenv ("PALANG_LSP_STDERR"), "1") != 0)
    flags |= G_SUBPROCESS_FLAGS_STDERR_SILENCE;

  launcher = g_subprocess_launcher_new (flags);
  if (self->root != NULL && g_file_is_native (self->root))
    {
      g_autofree char *root_path = g_file_get_path (self->root);

      g_subprocess_launcher_set_cwd (launcher, root_path);
    }

  self->process = g_subprocess_launcher_spawnv (launcher, (const char * const *) self->argv, &error);
  if (self->process == NULL)
    {
      g_autofree char *message = NULL;

      if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT) && self->install_hint != NULL)
        message = g_strdup_printf ("The %s language server “%s” isn't installed. Install it with: %s",
                                   self->title, self->argv[0], self->install_hint);
      else if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        message = g_strdup_printf ("The %s language server “%s” wasn't found.",
                                   self->title, self->argv[0]);
      else
        message = g_strdup_printf ("The %s language server “%s” couldn't be started: %s",
                                   self->title, self->argv[0], error->message);
      fail (self, message);
      return;
    }

  self->state = STATE_STARTING;
  self->cancellable = g_cancellable_new ();

  /* Our input is the server's stdout, our output its stdin. */
  stream = g_simple_io_stream_new (g_subprocess_get_stdout_pipe (self->process),
                                   g_subprocess_get_stdin_pipe (self->process));
  self->rpc = jsonrpc_client_new (stream);
  g_signal_connect (self->rpc, "notification", G_CALLBACK (on_rpc_notification), self);
  g_signal_connect (self->rpc, "handle-call", G_CALLBACK (on_rpc_handle_call), self);
  jsonrpc_client_start_listening (self->rpc);

  g_subprocess_wait_async (self->process, self->cancellable, on_process_exited, self);

  send_initialize (self);
}

/* ---- The server exited -------------------------------------------------- */

static void
finish_stop (PalangLspClient *self)
{
  g_clear_handle_id (&self->stop_timeout_id, g_source_remove);
  stop_process (self);
  self->state = STATE_STOPPED;

  if (self->held_application != NULL)
    g_application_release (self->held_application);
  g_clear_object (&self->held_application);

  /* The reference palang_lsp_client_stop took. */
  g_object_unref (self);
}

static void
on_process_exited (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  PalangLspClient *self = user_data;  /* not a reference; see the top */
  g_autoptr (GError) error = NULL;
  gint64 now = g_get_monotonic_time ();

  if (!g_subprocess_wait_finish (G_SUBPROCESS (source), result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Waiting for the %s language server: %s", self->title, error->message);
      return;
    }

  if (self->state == STATE_STOPPING)
    {
      finish_stop (self);
      return;
    }

  /* It crashed (or exited without being asked). */
  stop_process (self);

  if (self->n_crashes == 0 ||
      now - self->first_crash_time > RESTART_WINDOW_SECONDS * G_USEC_PER_SEC)
    {
      self->n_crashes = 0;
      self->first_crash_time = now;
    }
  self->n_crashes++;

  if (self->n_crashes > MAX_RESTARTS)
    {
      g_autofree char *message =
        g_strdup_printf ("The %s language server “%s” keeps stopping, so it won't be started "
                         "again. Use “Restart Language Servers” to try again.",
                         self->title, self->argv[0]);

      fail (self, message);
      return;
    }

  g_debug ("The %s language server stopped; starting it again", self->title);
  self->state = STATE_STOPPED;
  palang_lsp_client_start (self);
}

/* ---- Stopping ----------------------------------------------------------- */

static gboolean
on_stop_timeout (gpointer user_data)
{
  PalangLspClient *self = user_data;

  self->stop_timeout_id = 0;
  g_debug ("The %s language server didn't exit; killing it", self->title);
  /* on_process_exited follows and finishes the stop. */
  g_subprocess_force_exit (self->process);
  return G_SOURCE_REMOVE;
}

static void
on_shutdown_replied (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  PalangLspClient *self = user_data;  /* not a reference; see the top */
  g_autoptr (GError) error = NULL;

  if (!jsonrpc_client_call_finish (JSONRPC_CLIENT (source), result, NULL, &error) &&
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  /* Even if "shutdown" failed, "exit" is what makes it leave. */
  jsonrpc_client_send_notification_async (self->rpc, "exit", NULL, self->cancellable,
                                          NULL, NULL);
}

void
palang_lsp_client_stop (PalangLspClient *self)
{
  GApplication *application = g_application_get_default ();

  g_return_if_fail (PALANG_IS_LSP_CLIENT (self));

  if (self->state != STATE_STARTING && self->state != STATE_READY)
    {
      if (self->state == STATE_FAILED)
        self->state = STATE_STOPPED;
      return;
    }

  /* Released in finish_stop, when the process has exited. */
  g_object_ref (self);
  if (application != NULL)
    {
      self->held_application = g_object_ref (application);
      g_application_hold (application);
    }

  /* Not g_timeout_add_seconds (): that one rounds to whole-second ticks
   * (to save wake-ups) and can fire up to a second early. */
  self->stop_timeout_id = g_timeout_add (STOP_TIMEOUT_SECONDS * 1000, on_stop_timeout, self);

  /* "shutdown" is only allowed after "initialize" has been answered; a
   * server still starting is simply killed. */
  if (self->state == STATE_READY)
    jsonrpc_client_call_async (self->rpc, "shutdown", NULL, self->cancellable,
                               on_shutdown_replied, self);
  else
    g_subprocess_force_exit (self->process);

  self->state = STATE_STOPPING;
}

/* ---- Requests and notifications ----------------------------------------- */

static void
on_call_replied (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr (GTask) task = user_data;
  g_autoptr (GVariant) reply = NULL;
  GError *error = NULL;

  if (!jsonrpc_client_call_finish (JSONRPC_CLIENT (source), result, &reply, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_pointer (task, g_steal_pointer (&reply), (GDestroyNotify) g_variant_unref);
}

void
palang_lsp_client_call_async (PalangLspClient     *self,
                              const char          *method,
                              GVariant            *params,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;

  g_return_if_fail (PALANG_IS_LSP_CLIENT (self));
  g_return_if_fail (method != NULL);

  task = g_task_new (self, cancellable, callback, user_data);

  if (self->state != STATE_READY)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                               "The %s language server isn't running", self->title);
      return;
    }

  jsonrpc_client_call_async (self->rpc, method, params, cancellable,
                             on_call_replied, g_steal_pointer (&task));
}

GVariant *
palang_lsp_client_call_finish (PalangLspClient  *self,
                               GAsyncResult     *result,
                               GError          **error)
{
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
on_notification_sent (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  if (!jsonrpc_client_send_notification_finish (JSONRPC_CLIENT (source), result, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_debug ("Sending a notification to a language server: %s", error->message);
}

void
palang_lsp_client_notify (PalangLspClient *self,
                          const char      *method,
                          GVariant        *params)
{
  g_return_if_fail (PALANG_IS_LSP_CLIENT (self));
  g_return_if_fail (method != NULL);

  if (self->state != STATE_READY)
    return;

  jsonrpc_client_send_notification_async (self->rpc, method, params, self->cancellable,
                                          on_notification_sent, NULL);
}

/* ---- Getters ------------------------------------------------------------ */

gboolean
palang_lsp_client_is_ready (PalangLspClient *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_CLIENT (self), FALSE);

  return self->state == STATE_READY;
}

const char *
palang_lsp_client_get_title (PalangLspClient *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_CLIENT (self), NULL);

  return self->title;
}

GVariant *
palang_lsp_client_get_capabilities (PalangLspClient *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_CLIENT (self), NULL);

  return self->capabilities;
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_lsp_client_dispose (GObject *object)
{
  PalangLspClient *self = PALANG_LSP_CLIENT (object);

  /* A stop in progress holds a reference, so we only get here when there
   * is none; a server still running is killed. */
  stop_process (self);

  G_OBJECT_CLASS (palang_lsp_client_parent_class)->dispose (object);
}

static void
palang_lsp_client_finalize (GObject *object)
{
  PalangLspClient *self = PALANG_LSP_CLIENT (object);

  g_free (self->title);
  g_strfreev (self->argv);
  g_clear_object (&self->root);
  g_free (self->install_hint);

  G_OBJECT_CLASS (palang_lsp_client_parent_class)->finalize (object);
}

static void
palang_lsp_client_class_init (PalangLspClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = palang_lsp_client_dispose;
  object_class->finalize = palang_lsp_client_finalize;

  signals[SIGNAL_READY] =
    g_signal_new ("ready", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals[SIGNAL_FAILED] =
    g_signal_new ("failed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[SIGNAL_NOTIFICATION] =
    g_signal_new ("notification", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VARIANT);
}

static void
palang_lsp_client_init (PalangLspClient *self)
{
  self->state = STATE_STOPPED;
}

PalangLspClient *
palang_lsp_client_new (const char        *title,
                       const char * const *argv,
                       GFile             *root,
                       const char        *install_hint)
{
  PalangLspClient *self;

  g_return_val_if_fail (title != NULL, NULL);
  g_return_val_if_fail (argv != NULL && argv[0] != NULL, NULL);

  self = g_object_new (PALANG_TYPE_LSP_CLIENT, NULL);
  self->title = g_strdup (title);
  self->argv = g_strdupv ((char **) argv);
  self->root = root != NULL ? g_object_ref (root) : NULL;
  self->install_hint = g_strdup (install_hint);

  return self;
}
