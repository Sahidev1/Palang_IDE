/* palang-document.c — loading, saving and language detection for one file.
 *
 * The heavy lifting is done by GtkSourceView:
 *   GtkSourceFile        remembers the location, encoding (UTF-8, Latin-1…),
 *                        line endings (LF/CRLF) and compression of a file.
 *   GtkSourceFileLoader  reads a file into a buffer, detecting those.
 *   GtkSourceFileSaver   writes the buffer back using the same format.
 *
 * Our async functions wrap theirs in a GTask (see docs/00, section 5), so
 * callers get a single, simple async/finish pair per operation.
 */

#include "config.h"

#include <string.h>

#include "palang-document.h"

/* How much text to look at when guessing the language from file content
 * (e.g. a "#!/usr/bin/env python3" line in a file with no extension). */
#define CONTENT_SNIFF_LENGTH 1024

struct _PalangDocument
{
  GtkSourceBuffer parent_instance;

  /* Location + on-disk format. Always non-NULL; its location is NULL for a
   * new, never-saved document. */
  GtkSourceFile *source_file;
};

G_DEFINE_FINAL_TYPE (PalangDocument, palang_document, GTK_SOURCE_TYPE_BUFFER)

enum {
  PROP_0,
  PROP_FILE,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Picks the syntax-highlighting language from the file name and the start
 * of the text, or no highlighting if nothing matches. */
static void
update_language (PalangDocument *self)
{
  GtkSourceLanguageManager *languages = gtk_source_language_manager_get_default ();
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (self);
  GFile *location = gtk_source_file_get_location (self->source_file);
  g_autofree char *basename = NULL;
  g_autofree char *start_text = NULL;
  g_autofree char *content_type = NULL;
  GtkTextIter start, sniff_end;

  if (location == NULL)
    {
      gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self), NULL);
      return;
    }

  basename = g_file_get_basename (location);

  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_iter_at_offset (buffer, &sniff_end, CONTENT_SNIFF_LENGTH);
  start_text = gtk_text_buffer_get_text (buffer, &start, &sniff_end, FALSE);

  content_type = g_content_type_guess (basename,
                                       (const guchar *) start_text, strlen (start_text),
                                       NULL);

  gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self),
                                  gtk_source_language_manager_guess_language (languages,
                                                                              basename,
                                                                              content_type));
}

/* Tells listeners (tab labels, window title) that the location changed. */
static void
notify_location_changed (PalangDocument *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

/* ---- Loading ------------------------------------------------------------ */

/* State for one load, stored as the GTask's "task data". */
typedef struct {
  GtkSourceFile   *file;     /* new location + detected format */
  GtkSourceBuffer *scratch;  /* temporary buffer the file is read into */
  gboolean         utf8_failed;  /* retrying without UTF-8, see below */
} LoadData;

static void
load_data_free (LoadData *data)
{
  g_clear_object (&data->file);
  g_clear_object (&data->scratch);
  g_free (data);
}

/* Replaces the document's text with the scratch buffer's text. Wrapped in an
 * "irreversible action" so Undo can't go back to before the load. */
static void
take_text_from (PalangDocument  *self,
                GtkSourceBuffer *scratch)
{
  GtkTextIter start, end;
  g_autofree char *text = NULL;

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (scratch), &start, &end);
  text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (scratch), &start, &end, TRUE);

  gtk_text_buffer_begin_irreversible_action (GTK_TEXT_BUFFER (self));
  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (self), text, -1);
  gtk_text_buffer_end_irreversible_action (GTK_TEXT_BUFFER (self));

  gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (self), FALSE);
}

static void on_load_finished (GObject      *source,
                              GAsyncResult *result,
                              gpointer      user_data);

/* Starts reading the file into the scratch buffer. Takes over the caller's
 * reference to @task, which on_load_finished() then owns. */
static void
start_loader (GTask *task)
{
  LoadData *data = g_task_get_task_data (task);
  g_autoptr (GtkSourceFileLoader) loader = NULL;
  g_autoptr (GSList) candidates = NULL;

  loader = gtk_source_file_loader_new (data->scratch, data->file);

  /* Encodings to try, in order. Valid UTF-8 is by far the most common; if
   * the file isn't, ISO-8859-15 (Western European) accepts any bytes, so it
   * works as a fallback that never fails. The saver later writes the file
   * back in whichever encoding was detected. */
  if (!data->utf8_failed)
    candidates = g_slist_append (candidates, (gpointer) gtk_source_encoding_get_utf8 ());
  candidates = g_slist_append (candidates,
                               (gpointer) gtk_source_encoding_get_from_charset ("ISO-8859-15"));
  gtk_source_file_loader_set_candidate_encodings (loader, candidates);

  /* The loader keeps itself alive until the load completes. */
  gtk_source_file_loader_load_async (loader, G_PRIORITY_DEFAULT,
                                     g_task_get_cancellable (task),
                                     NULL, NULL, NULL,
                                     on_load_finished, task);
}

static void
on_load_finished (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  GtkSourceFileLoader *loader = GTK_SOURCE_FILE_LOADER (source);
  /* We own the task's reference from here on; g_autoptr drops it when this
   * function returns, after the task has delivered its result. */
  g_autoptr (GTask) task = user_data;
  PalangDocument *self = g_task_get_source_object (task);
  LoadData *data = g_task_get_task_data (task);
  g_autoptr (GError) error = NULL;
  GtkTextIter start;

  if (!gtk_source_file_loader_load_finish (loader, result, &error))
    {
      /* GtkSourceView quirk: when a non-UTF-8 byte sits in the last few bytes
       * of a file, it is taken as the start of an unfinished UTF-8 character,
       * so the loader settles on UTF-8 and inserts a replacement character
       * ("conversion fallback"). Saving that would corrupt the file, so we
       * retry once without UTF-8. */
      if (g_error_matches (error, GTK_SOURCE_FILE_LOADER_ERROR,
                           GTK_SOURCE_FILE_LOADER_ERROR_CONVERSION_FALLBACK) &&
          !data->utf8_failed)
        {
          data->utf8_failed = TRUE;
          start_loader (g_steal_pointer (&task));
          return;
        }

      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Only now that loading worked does the document take the new text,
   * location and format. */
  take_text_from (self, data->scratch);
  g_set_object (&self->source_file, data->file);

  update_language (self);

  gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self), &start);
  gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (self), &start);

  notify_location_changed (self);
  g_task_return_boolean (task, TRUE);
}

void
palang_document_load_async (PalangDocument      *self,
                            GFile               *file,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  LoadData *data;

  g_return_if_fail (PALANG_IS_DOCUMENT (self));
  g_return_if_fail (G_IS_FILE (file));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, palang_document_load_async);

  /* GtkSourceFileLoader empties its buffer *before* it knows whether the
   * file can be read. So we load into a temporary buffer and a fresh
   * GtkSourceFile; a failed load then leaves the document untouched. */
  data = g_new0 (LoadData, 1);
  data->file = gtk_source_file_new ();
  data->scratch = gtk_source_buffer_new (NULL);
  gtk_source_file_set_location (data->file, file);
  gtk_source_buffer_set_implicit_trailing_newline (data->scratch, FALSE);
  g_task_set_task_data (task, data, (GDestroyNotify) load_data_free);

  start_loader (g_steal_pointer (&task));
}

gboolean
palang_document_load_finish (PalangDocument  *self,
                             GAsyncResult    *result,
                             GError         **error)
{
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/* ---- Saving ------------------------------------------------------------- */

static void
on_save_finished (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  GtkSourceFileSaver *saver = GTK_SOURCE_FILE_SAVER (source);
  g_autoptr (GTask) task = user_data;
  PalangDocument *self = g_task_get_source_object (task);
  g_autoptr (GError) error = NULL;

  if (!gtk_source_file_saver_save_finish (saver, result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* After "Save As" the file name may have changed (e.g. notes.txt →
   * notes.py), so the language may change too. */
  update_language (self);
  notify_location_changed (self);
  g_task_return_boolean (task, TRUE);
}

void
palang_document_save_async (PalangDocument      *self,
                            GFile               *target,
                            gboolean             force,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (GtkSourceFileSaver) saver = NULL;
  GFile *location;

  g_return_if_fail (PALANG_IS_DOCUMENT (self));
  g_return_if_fail (target == NULL || G_IS_FILE (target));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, palang_document_save_async);

  location = gtk_source_file_get_location (self->source_file);

  if (target == NULL && location == NULL)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                               "The document has no file yet; use Save As");
      return;
    }

  /* "with_target" saves to a new location; on success the saver updates
   * self->source_file to point there. */
  if (target != NULL)
    saver = gtk_source_file_saver_new_with_target (GTK_SOURCE_BUFFER (self),
                                                   self->source_file, target);
  else
    saver = gtk_source_file_saver_new (GTK_SOURCE_BUFFER (self), self->source_file);

  if (force)
    gtk_source_file_saver_set_flags (saver,
                                     GTK_SOURCE_FILE_SAVER_FLAGS_IGNORE_MODIFICATION_TIME);

  gtk_source_file_saver_save_async (saver, G_PRIORITY_DEFAULT, cancellable,
                                    NULL, NULL, NULL,
                                    on_save_finished, g_steal_pointer (&task));
}

gboolean
palang_document_save_finish (PalangDocument  *self,
                             GAsyncResult    *result,
                             GError         **error)
{
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/* ---- Accessors and GObject boilerplate ---------------------------------- */

GFile *
palang_document_get_file (PalangDocument *self)
{
  g_return_val_if_fail (PALANG_IS_DOCUMENT (self), NULL);

  return gtk_source_file_get_location (self->source_file);
}

char *
palang_document_dup_title (PalangDocument *self)
{
  GFile *location;

  g_return_val_if_fail (PALANG_IS_DOCUMENT (self), NULL);

  location = gtk_source_file_get_location (self->source_file);
  if (location == NULL)
    return g_strdup ("Untitled");

  return g_file_get_basename (location);
}

static void
palang_document_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PalangDocument *self = PALANG_DOCUMENT (object);

  switch (prop_id)
    {
    case PROP_FILE:
      g_value_set_object (value, palang_document_get_file (self));
      break;

    case PROP_TITLE:
      /* take_string: the GValue takes ownership of the new string. */
      g_value_take_string (value, palang_document_dup_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* "constructed" runs after all construction properties have been applied.
 * Setting implicit-trailing-newline in _init() wouldn't stick: it's a
 * construction property, and GObject sets its default (TRUE) after _init(). */
static void
palang_document_constructed (GObject *object)
{
  G_OBJECT_CLASS (palang_document_parent_class)->constructed (object);

  /* By default GtkSourceBuffer hides a file's final newline and always adds
   * one back on save, which silently changes files that don't end with one.
   * Turning it off keeps files byte-for-byte as they were. */
  gtk_source_buffer_set_implicit_trailing_newline (GTK_SOURCE_BUFFER (object), FALSE);
}

static void
palang_document_finalize (GObject *object)
{
  PalangDocument *self = PALANG_DOCUMENT (object);

  g_clear_object (&self->source_file);

  G_OBJECT_CLASS (palang_document_parent_class)->finalize (object);
}

static void
palang_document_class_init (PalangDocumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = palang_document_constructed;
  object_class->get_property = palang_document_get_property;
  object_class->finalize = palang_document_finalize;

  /* Read-only properties, so other code can watch them with
   * "notify::file" / "notify::title" or bind them to widgets. */
  properties[PROP_FILE] =
    g_param_spec_object ("file", NULL, NULL,
                         G_TYPE_FILE,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
palang_document_init (PalangDocument *self)
{
  self->source_file = gtk_source_file_new ();
}

PalangDocument *
palang_document_new (void)
{
  return g_object_new (PALANG_TYPE_DOCUMENT, NULL);
}
