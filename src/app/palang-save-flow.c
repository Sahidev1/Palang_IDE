/* palang-save-flow.c — the steps of saving a document with dialogs.
 *
 * One save is a small chain of async steps, all sharing one GTask:
 *
 *   [ask_for_location] → save_to → done
 *                           ↓ file changed on disk?
 *                       ask_overwrite → save_to (forced) → done
 *
 * The GTask's reference is passed from step to step; whichever step
 * finishes the task (g_task_return_*) also drops it.
 */

#include "config.h"

#include <adwaita.h>

#include "palang-save-flow.h"
#include "palang-window.h"

typedef struct {
  GtkWindow *parent;  /* owned */
  GFile     *target;  /* owned; NULL = the document's current file */
} SaveData;

static void
save_data_free (SaveData *data)
{
  g_clear_object (&data->parent);
  g_clear_object (&data->target);
  g_free (data);
}

static void
return_cancelled (GTask *task)
{
  g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                           "Saving was cancelled");
  g_object_unref (task);
}

/* ---- Step: overwrite a file another program changed --------------------- */

static void save_to (GTask *task, gboolean force);

static void
on_overwrite_response (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  GTask *task = user_data;
  const char *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (source), result);

  if (g_strcmp0 (response, "overwrite") == 0)
    save_to (task, TRUE);
  else
    return_cancelled (task);
}

static void
ask_overwrite (GTask *task)
{
  PalangDocument *document = g_task_get_source_object (task);
  SaveData *data = g_task_get_task_data (task);
  g_autofree char *title = palang_document_dup_title (document);
  AdwDialog *dialog;

  dialog = adw_alert_dialog_new ("File Changed on Disk", NULL);
  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                                "“%s” was changed by another program after it was "
                                "opened. Saving now will overwrite those changes.",
                                title);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", "_Cancel",
                                  "overwrite", "_Overwrite",
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "overwrite",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  /* choose() shows the dialog and takes ownership of it. */
  adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (data->parent),
                           g_task_get_cancellable (task),
                           on_overwrite_response, task);
}

/* ---- Step: write the file ----------------------------------------------- */

static void
on_saved (GObject      *source,
          GAsyncResult *result,
          gpointer      user_data)
{
  GTask *task = user_data;
  g_autoptr (GError) error = NULL;

  if (palang_document_save_finish (PALANG_DOCUMENT (source), result, &error))
    {
      g_task_return_boolean (task, TRUE);
      g_object_unref (task);
      return;
    }

  if (g_error_matches (error, GTK_SOURCE_FILE_SAVER_ERROR,
                       GTK_SOURCE_FILE_SAVER_ERROR_EXTERNALLY_MODIFIED))
    {
      ask_overwrite (task);
      return;
    }

  g_task_return_error (task, g_steal_pointer (&error));
  g_object_unref (task);
}

static void
save_to (GTask    *task,
         gboolean  force)
{
  PalangDocument *document = g_task_get_source_object (task);
  SaveData *data = g_task_get_task_data (task);

  palang_document_save_async (document, data->target, force,
                              g_task_get_cancellable (task),
                              on_saved, task);
}

/* ---- Step: choose where to save ----------------------------------------- */

static void
on_location_chosen (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  GTask *task = user_data;
  SaveData *data = g_task_get_task_data (task);
  g_autoptr (GError) error = NULL;

  data->target = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), result, &error);

  /* NULL means the user closed the file chooser without picking a file. */
  if (data->target == NULL)
    {
      return_cancelled (task);
      return;
    }

  save_to (task, FALSE);
}

static void
ask_for_location (GTask *task)
{
  PalangDocument *document = g_task_get_source_object (task);
  SaveData *data = g_task_get_task_data (task);
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
  GFile *current = palang_document_get_file (document);

  gtk_file_dialog_set_title (dialog, "Save As");

  /* Start in the file's folder with its name filled in; for a new document,
   * suggest a name and start in the window's project folder, if one is
   * open (else the file chooser picks, usually the last folder used). The
   * file chooser itself asks before replacing an existing file. */
  if (current != NULL)
    {
      gtk_file_dialog_set_initial_file (dialog, current);
    }
  else
    {
      g_autofree char *title = palang_document_dup_title (document);
      GFile *folder = NULL;

      if (PALANG_IS_WINDOW (data->parent))
        folder = palang_window_get_folder (PALANG_WINDOW (data->parent));

      gtk_file_dialog_set_initial_name (dialog, title);
      if (folder != NULL)
        gtk_file_dialog_set_initial_folder (dialog, folder);
    }

  gtk_file_dialog_save (dialog, data->parent, g_task_get_cancellable (task),
                        on_location_chosen, task);
}

/* ---- Public API --------------------------------------------------------- */

void
palang_save_document_async (GtkWindow           *parent,
                            PalangDocument      *document,
                            gboolean             ask_location,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  GTask *task;
  SaveData *data;

  g_return_if_fail (GTK_IS_WINDOW (parent));
  g_return_if_fail (PALANG_IS_DOCUMENT (document));

  task = g_task_new (document, cancellable, callback, user_data);
  g_task_set_source_tag (task, palang_save_document_async);

  data = g_new0 (SaveData, 1);
  data->parent = g_object_ref (parent);
  g_task_set_task_data (task, data, (GDestroyNotify) save_data_free);

  if (ask_location || palang_document_get_file (document) == NULL)
    ask_for_location (task);
  else
    save_to (task, FALSE);
}

gboolean
palang_save_document_finish (PalangDocument  *document,
                             GAsyncResult    *result,
                             GError         **error)
{
  g_return_val_if_fail (g_task_is_valid (result, document), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
