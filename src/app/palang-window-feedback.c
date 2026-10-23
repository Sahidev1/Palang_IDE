/* palang-window-feedback.c — PalangWindow's complaint box.
 *
 * Part of PalangWindow (see palang-window-private.h for how the window's
 * code is split). Ctrl+Shift+F1, win.complain, opens a PalangComplaintDialog
 * (feedback/) that writes to the file of the `complaints-file` setting,
 * noting the file and line the cursor is on; a toast confirms it was saved.
 */

#include "config.h"

#include "palang-complaint-dialog.h"
#include "palang-complaints.h"
#include "palang-settings.h"
#include "palang-window-private.h"

/* Returns: (transfer full) (nullable): what the complaint is probably
 * about: the active tab's file and the cursor's line ("src/main.c:42"),
 * or NULL when no file is open. */
static char *
dup_context (PalangWindow *self)
{
  PalangEditor *editor = palang_window_get_active_editor (self);
  PalangDocument *document;
  GFile *file;
  GFile *folder;
  GtkTextIter cursor;
  g_autofree char *name = NULL;

  if (editor == NULL)
    return NULL;

  document = palang_editor_get_document (editor);
  file = palang_document_get_file (document);
  folder = palang_window_get_folder (self);

  /* Inside the project folder: relative to it, like "src/main.c". Else
   * "~/…" (palang_editor_dup_path ()), or "Untitled" if never saved. */
  if (file != NULL && folder != NULL)
    name = g_file_get_relative_path (folder, file);
  if (name == NULL)
    name = palang_editor_dup_path (editor);
  if (name == NULL)
    name = palang_document_dup_title (document);

  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (document), &cursor,
                                    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (document)));
  /* Lines count from 0 in GTK, from 1 for people. */
  return g_strdup_printf ("%s:%d", name, gtk_text_iter_get_line (&cursor) + 1);
}

static void
on_complaint_sent (PalangComplaintDialog *dialog,
                   PalangWindow          *self)
{
  _palang_window_show_toast (self, "Complaint saved");
}

static void
complain_action (GtkWidget  *widget,
                 const char *action_name,
                 GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  g_autofree char *path = NULL;
  g_autofree char *context = NULL;
  g_autoptr (GFile) file = NULL;
  PalangComplaintDialog *dialog;

  /* Pressing the shortcut again while the box is open does nothing. */
  if (PALANG_IS_COMPLAINT_DIALOG (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (self))))
    return;

  g_object_get (palang_settings_get_default (), "complaints-file", &path, NULL);
  file = palang_complaints_get_file (path);
  context = dup_context (self);

  dialog = palang_complaint_dialog_new (file, context);
  g_signal_connect (dialog, "sent", G_CALLBACK (on_complaint_sent), self);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

void
_palang_window_class_init_feedback (GtkWidgetClass *widget_class)
{
  gtk_widget_class_install_action (widget_class, "win.complain", NULL, complain_action);
}
