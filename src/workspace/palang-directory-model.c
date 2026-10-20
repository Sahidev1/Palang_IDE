/* palang-directory-model.c — read a folder, then follow its changes.
 *
 * The entries live in a GListStore; this object implements the GListModel
 * interface by passing calls through to it.
 *
 * Loading:  g_file_enumerate_children_async, then next_files_async in
 *           batches until the folder is fully read.
 * Watching: a GFileMonitor with WATCH_MOVES, so renames arrive as one
 *           RENAMED event that names both the old and the new file.
 *
 * Entries are matched by name, so an event for a name that's already listed
 * (e.g. while the initial read is still running) never adds a duplicate.
 */

#include "config.h"

#include "palang-directory-model.h"

/* Entries read per batch while loading. */
#define BATCH_SIZE 200

struct _PalangDirectoryModel
{
  GObject parent_instance;

  GFile        *directory;    /* owned */
  char         *attributes;   /* owned */
  GListStore   *entries;      /* owned; GFileInfo items */
  GFileMonitor *monitor;      /* owned, nullable */
  GCancellable *cancellable;  /* owned; cancelled on dispose */
};

static void list_model_iface_init (GListModelInterface *iface);

/* _WITH_CODE lets us declare that this type implements GListModel. */
G_DEFINE_FINAL_TYPE_WITH_CODE (PalangDirectoryModel, palang_directory_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

/* ---- GListModel: pass everything through to the store ------------------- */

static GType
palang_directory_model_get_item_type (GListModel *model)
{
  return G_TYPE_FILE_INFO;
}

static guint
palang_directory_model_get_n_items (GListModel *model)
{
  return g_list_model_get_n_items (G_LIST_MODEL (PALANG_DIRECTORY_MODEL (model)->entries));
}

static gpointer
palang_directory_model_get_item (GListModel *model,
                                 guint       position)
{
  return g_list_model_get_item (G_LIST_MODEL (PALANG_DIRECTORY_MODEL (model)->entries), position);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = palang_directory_model_get_item_type;
  iface->get_n_items = palang_directory_model_get_n_items;
  iface->get_item = palang_directory_model_get_item;
}

/* The store changed: tell whoever watches us (e.g. the tree view). */
static void
on_entries_changed (GListModel           *entries,
                    guint                 position,
                    guint                 removed,
                    guint                 added,
                    PalangDirectoryModel *self)
{
  g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
}

/* ---- Adding and removing entries ---------------------------------------- */

/* Returns: the position of the entry called @name, or -1. */
static int
find_entry (PalangDirectoryModel *self,
            const char           *name)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->entries));

  for (guint i = 0; i < n; i++)
    {
      g_autoptr (GFileInfo) info = g_list_model_get_item (G_LIST_MODEL (self->entries), i);

      if (g_strcmp0 (g_file_info_get_name (info), name) == 0)
        return (int) i;
    }

  return -1;
}

/* Adds @info, or replaces the entry with the same name. */
static void
add_entry (PalangDirectoryModel *self,
           GFileInfo            *info)
{
  const char *name = g_file_info_get_name (info);
  g_autoptr (GFile) file = g_file_get_child (self->directory, name);
  int position;

  g_file_info_set_attribute_object (info, "standard::file", G_OBJECT (file));

  position = find_entry (self, name);
  if (position >= 0)
    g_list_store_splice (self->entries, position, 1, (gpointer *) &info, 1);
  else
    g_list_store_append (self->entries, info);
}

static void
remove_entry (PalangDirectoryModel *self,
              GFile                *file)
{
  g_autofree char *name = g_file_get_basename (file);
  int position = find_entry (self, name);

  if (position >= 0)
    g_list_store_remove (self->entries, position);
}

/* ---- Watching for changes ----------------------------------------------- */

static void
on_info_queried (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr (PalangDirectoryModel) self = user_data;
  g_autoptr (GFileInfo) info = NULL;

  /* Fails if the file was already removed again, or if we were disposed
   * (cancelled); either way there's nothing to add. */
  info = g_file_query_info_finish (G_FILE (source), result, NULL);
  if (info != NULL)
    add_entry (self, info);
}

/* Reads @file's details in the background, then adds it. */
static void
query_and_add (PalangDirectoryModel *self,
               GFile                *file)
{
  g_file_query_info_async (file, self->attributes, G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT, self->cancellable,
                           on_info_queried, g_object_ref (self));
}

static void
on_directory_changed (GFileMonitor         *monitor,
                      GFile                *file,
                      GFile                *other_file,
                      GFileMonitorEvent     event,
                      PalangDirectoryModel *self)
{
  switch (event)
    {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
      query_and_add (self, file);
      break;

    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
      remove_entry (self, file);
      break;

    case G_FILE_MONITOR_EVENT_RENAMED:
      /* Within this folder: @file is the old name, @other_file the new. */
      remove_entry (self, file);
      query_and_add (self, other_file);
      break;

    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
    case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
    case G_FILE_MONITOR_EVENT_UNMOUNTED:
    case G_FILE_MONITOR_EVENT_MOVED:
    default:
      /* Content changes don't affect the list; MOVED is only sent without
       * WATCH_MOVES. */
      break;
    }
}

/* ---- Initial loading ---------------------------------------------------- */

static void
on_next_files (GObject      *source,
               GAsyncResult *result,
               gpointer      user_data)
{
  GFileEnumerator *enumerator = G_FILE_ENUMERATOR (source);
  g_autoptr (PalangDirectoryModel) self = user_data;
  GList *infos;

  /* An empty list means the folder has been read completely (or an error
   * happened, which we treat the same: show what we have). */
  infos = g_file_enumerator_next_files_finish (enumerator, result, NULL);
  if (infos == NULL)
    return;

  for (GList *l = infos; l != NULL; l = l->next)
    add_entry (self, l->data);
  g_list_free_full (infos, g_object_unref);

  g_file_enumerator_next_files_async (enumerator, BATCH_SIZE, G_PRIORITY_DEFAULT,
                                      self->cancellable, on_next_files,
                                      g_object_ref (self));
}

static void
on_enumerated (GObject      *source,
               GAsyncResult *result,
               gpointer      user_data)
{
  g_autoptr (PalangDirectoryModel) self = user_data;
  g_autoptr (GFileEnumerator) enumerator = NULL;

  /* NULL if the folder can't be read (no permission, deleted, cancelled):
   * the list just stays empty. */
  enumerator = g_file_enumerate_children_finish (G_FILE (source), result, NULL);
  if (enumerator == NULL)
    return;

  /* The pending operation keeps the enumerator alive. */
  g_file_enumerator_next_files_async (enumerator, BATCH_SIZE, G_PRIORITY_DEFAULT,
                                      self->cancellable, on_next_files,
                                      g_object_ref (self));
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_directory_model_dispose (GObject *object)
{
  PalangDirectoryModel *self = PALANG_DIRECTORY_MODEL (object);

  g_cancellable_cancel (self->cancellable);

  if (self->monitor != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->monitor, self);
      g_file_monitor_cancel (self->monitor);
      g_clear_object (&self->monitor);
    }

  G_OBJECT_CLASS (palang_directory_model_parent_class)->dispose (object);
}

static void
palang_directory_model_finalize (GObject *object)
{
  PalangDirectoryModel *self = PALANG_DIRECTORY_MODEL (object);

  g_clear_object (&self->entries);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->directory);
  g_free (self->attributes);

  G_OBJECT_CLASS (palang_directory_model_parent_class)->finalize (object);
}

static void
palang_directory_model_class_init (PalangDirectoryModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = palang_directory_model_dispose;
  object_class->finalize = palang_directory_model_finalize;
}

static void
palang_directory_model_init (PalangDirectoryModel *self)
{
  self->entries = g_list_store_new (G_TYPE_FILE_INFO);
  self->cancellable = g_cancellable_new ();

  g_signal_connect (self->entries, "items-changed",
                    G_CALLBACK (on_entries_changed), self);
}

PalangDirectoryModel *
palang_directory_model_new (GFile      *directory,
                            const char *attributes)
{
  PalangDirectoryModel *self;

  g_return_val_if_fail (G_IS_FILE (directory), NULL);
  g_return_val_if_fail (attributes != NULL, NULL);

  self = g_object_new (PALANG_TYPE_DIRECTORY_MODEL, NULL);
  self->directory = g_object_ref (directory);
  /* We always need the name, to match monitor events to entries. */
  self->attributes = g_strconcat ("standard::name,", attributes, NULL);

  /* Start watching before reading, so nothing created in between is
   * missed. (Duplicates can't happen: entries are matched by name.) */
  self->monitor = g_file_monitor_directory (directory, G_FILE_MONITOR_WATCH_MOVES,
                                            NULL, NULL);
  if (self->monitor != NULL)
    g_signal_connect (self->monitor, "changed",
                      G_CALLBACK (on_directory_changed), self);

  g_file_enumerate_children_async (directory, self->attributes, G_FILE_QUERY_INFO_NONE,
                                   G_PRIORITY_DEFAULT, self->cancellable,
                                   on_enumerated, g_object_ref (self));

  return self;
}
