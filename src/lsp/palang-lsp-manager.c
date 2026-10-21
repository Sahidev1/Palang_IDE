/* palang-lsp-manager.c — the language servers of one window.
 *
 * Two collections:
 *   clients    server key ("python") → PalangLspClient, for the servers
 *              that have been started (including failed ones, so a server
 *              that isn't installed is reported once, not for every file)
 *   documents  the open documents; their languages decide which servers
 *              run
 *
 * Whenever something changes the set of servers (folder, settings,
 * restart), the affected clients are stopped and removed, then
 * ensure_client () runs for every document and starts what's missing.
 */

#include "config.h"

#include "palang-lsp-languages.h"
#include "palang-lsp-manager.h"

struct _PalangLspManager
{
  GObject parent_instance;

  PalangSettings *settings;  /* owned */
  GFile          *folder;    /* owned, nullable */

  /* Server key (static strings from palang-lsp-languages.c) → owned
   * PalangLspClient. */
  GHashTable *clients;

  GPtrArray *documents;  /* owned references to PalangDocument */
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

/* Starts the server @document needs, unless it is running (or has failed)
 * already. */
static void
ensure_client (PalangLspManager *self,
               PalangDocument   *document)
{
  const PalangLspServerInfo *server = find_server (document);
  g_autofree char *setting = NULL;
  g_autofree char *command = NULL;
  g_autofree char *default_command = NULL;
  g_auto (GStrv) argv = NULL;
  g_autoptr (GError) error = NULL;
  PalangLspClient *client;

  if (server == NULL || g_hash_table_contains (self->clients, server->key))
    return;

  setting = palang_lsp_dup_setting_name (server);
  command = palang_settings_dup_value (self->settings, setting);
  if (command == NULL || g_strstrip (command)[0] == '\0')
    return;  /* the user turned this server off */

  /* "typescript-language-server --stdio" → its words, split the way a
   * shell would (so quotes work for paths with spaces). */
  if (!g_shell_parse_argv (command, NULL, &argv, &error))
    {
      g_autofree char *message =
        g_strdup_printf ("The setting “%s” isn't a valid command: %s", setting, error->message);

      g_signal_emit (self, signals[SIGNAL_SERVER_FAILED], 0, message);
      return;
    }

  /* The install hint is only right for the server Palang suggests. */
  default_command = palang_settings_dup_default_value (setting);
  client = palang_lsp_client_new (server->title, (const char * const *) argv, self->folder,
                                  g_strcmp0 (command, default_command) == 0 ? server->install_hint
                                                                            : NULL);
  g_signal_connect (client, "failed", G_CALLBACK (on_client_failed), self);
  g_hash_table_insert (self->clients, (gpointer) server->key, client);

  palang_lsp_client_start (client);
}

static void
ensure_all_clients (PalangLspManager *self)
{
  for (guint i = 0; i < self->documents->len; i++)
    ensure_client (self, g_ptr_array_index (self->documents, i));
}

/* Stops @client; the hash table's reference is dropped by the caller. The
 * client stays alive by itself until its server has exited. */
static void
stop_client (PalangLspManager *self,
             PalangLspClient  *client)
{
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

static void
on_document_language_changed (PalangDocument   *document,
                              GParamSpec       *pspec,
                              PalangLspManager *self)
{
  ensure_client (self, document);
}

void
palang_lsp_manager_add_document (PalangLspManager *self,
                                 PalangDocument   *document)
{
  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));
  g_return_if_fail (PALANG_IS_DOCUMENT (document));

  if (g_ptr_array_find (self->documents, document, NULL))
    return;

  g_ptr_array_add (self->documents, g_object_ref (document));
  g_signal_connect (document, "notify::language",
                    G_CALLBACK (on_document_language_changed), self);
  ensure_client (self, document);
}

void
palang_lsp_manager_remove_document (PalangLspManager *self,
                                    PalangDocument   *document)
{
  g_return_if_fail (PALANG_IS_LSP_MANAGER (self));
  g_return_if_fail (PALANG_IS_DOCUMENT (document));

  if (!g_ptr_array_find (self->documents, document, NULL))
    return;

  g_signal_handlers_disconnect_by_data (document, self);
  /* The array's free function drops our reference. A server keeps running
   * when its last document closes: opening another file of that language
   * is likely, and starting a server can take seconds. */
  g_ptr_array_remove (self->documents, document);
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

  /* Each server is asked to exit; the clients outlive us until it has. */
  if (self->clients != NULL)
    stop_all_clients (self);
  g_clear_pointer (&self->clients, g_hash_table_unref);

  if (self->documents != NULL)
    for (guint i = 0; i < self->documents->len; i++)
      g_signal_handlers_disconnect_by_data (g_ptr_array_index (self->documents, i), self);
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
