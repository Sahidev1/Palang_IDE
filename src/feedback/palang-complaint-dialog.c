/* palang-complaint-dialog.c — the complaint box (see the header).
 *
 * Layout is in data/ui/palang-complaint-dialog.ui. Sending is a widget
 * action, "complaint.send": the Send button and Ctrl+Enter both trigger
 * it, and turning the action off (while the text is empty) greys out the
 * button and silences the shortcut in one go.
 */

#include "config.h"

#include "palang-complaint-dialog.h"
#include "palang-complaints.h"

struct _PalangComplaintDialog
{
  AdwDialog parent_instance;

  GFile *file;     /* owned: where complaints go */
  char  *context;  /* owned, nullable: "src/main.c:42" */

  /* Template children */
  GtkLabel    *context_label;
  GtkTextView *text_view;
  GtkLabel    *error_label;
  GtkLabel    *file_label;
};

G_DEFINE_FINAL_TYPE (PalangComplaintDialog, palang_complaint_dialog, ADW_TYPE_DIALOG)

enum {
  SIGNAL_SENT,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Returns: (transfer full): the text typed so far. */
static char *
dup_text (PalangComplaintDialog *self)
{
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (self->text_view);
  GtkTextIter start, end;

  gtk_text_buffer_get_bounds (buffer, &start, &end);
  return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

/* Only something other than blank space can be sent. */
static void
on_text_changed (GtkTextBuffer         *buffer,
                 PalangComplaintDialog *self)
{
  g_autofree char *text = dup_text (self);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "complaint.send",
                                 g_strstrip (text)[0] != '\0');
}

static void
send_action (GtkWidget  *widget,
             const char *action_name,
             GVariant   *parameter)
{
  PalangComplaintDialog *self = PALANG_COMPLAINT_DIALOG (widget);
  g_autofree char *text = dup_text (self);
  g_autoptr (GDateTime) now = g_date_time_new_now_local ();
  g_autofree char *entry = palang_complaints_format_entry (now, self->context, text);
  g_autoptr (GError) error = NULL;

  if (!palang_complaints_append (self->file, entry, &error))
    {
      /* Keep the dialog and the text, so the complaint isn't lost. */
      g_autofree char *message = g_strdup_printf ("Couldn't save it: %s", error->message);

      gtk_label_set_text (self->error_label, message);
      gtk_widget_set_visible (GTK_WIDGET (self->error_label), TRUE);
      return;
    }

  g_signal_emit (self, signals[SIGNAL_SENT], 0);
  adw_dialog_close (ADW_DIALOG (self));
}

/* Returns: (transfer full): @file as "~/…" when it is in the home folder. */
static char *
dup_display_path (GFile *file)
{
  g_autoptr (GFile) home = g_file_new_for_path (g_get_home_dir ());
  g_autofree char *relative = g_file_get_relative_path (home, file);

  if (relative != NULL)
    return g_build_filename ("~", relative, NULL);

  return g_file_get_parse_name (file);
}

static void
palang_complaint_dialog_dispose (GObject *object)
{
  PalangComplaintDialog *self = PALANG_COMPLAINT_DIALOG (object);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_COMPLAINT_DIALOG);

  G_OBJECT_CLASS (palang_complaint_dialog_parent_class)->dispose (object);
}

static void
palang_complaint_dialog_finalize (GObject *object)
{
  PalangComplaintDialog *self = PALANG_COMPLAINT_DIALOG (object);

  g_clear_object (&self->file);
  g_free (self->context);

  G_OBJECT_CLASS (palang_complaint_dialog_parent_class)->finalize (object);
}

static void
palang_complaint_dialog_class_init (PalangComplaintDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = palang_complaint_dialog_dispose;
  object_class->finalize = palang_complaint_dialog_finalize;

  signals[SIGNAL_SENT] =
    g_signal_new ("sent", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  gtk_widget_class_install_action (widget_class, "complaint.send", NULL, send_action);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-complaint-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangComplaintDialog, context_label);
  gtk_widget_class_bind_template_child (widget_class, PalangComplaintDialog, text_view);
  gtk_widget_class_bind_template_child (widget_class, PalangComplaintDialog, error_label);
  gtk_widget_class_bind_template_child (widget_class, PalangComplaintDialog, file_label);
}

/* Ctrl+Enter (either Enter key) sends. Returns TRUE: the key is used up,
 * so the text box doesn't also start a new line. */
static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                PalangComplaintDialog *self)
{
  if ((keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter) ||
      (state & GDK_CONTROL_MASK) == 0)
    return FALSE;

  /* Does nothing while the action is off (blank text). */
  gtk_widget_activate_action (GTK_WIDGET (self), "complaint.send", NULL);
  return TRUE;
}

static void
palang_complaint_dialog_init (PalangComplaintDialog *self)
{
  GtkEventController *keys;

  gtk_widget_init_template (GTK_WIDGET (self));

  /* The text box would take Ctrl+Enter as a new line before a shortcut of
   * the dialog saw it. A key controller in the *capture* phase runs on the
   * way down from the window to the focused widget, so the dialog's runs
   * before the text box's. */
  keys = gtk_event_controller_key_new ();
  gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
  g_signal_connect (keys, "key-pressed", G_CALLBACK (on_key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);

  g_signal_connect (gtk_text_view_get_buffer (self->text_view), "changed",
                    G_CALLBACK (on_text_changed), self);
  /* Nothing typed yet. */
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "complaint.send", FALSE);
}

PalangComplaintDialog *
palang_complaint_dialog_new (GFile      *file,
                             const char *context)
{
  PalangComplaintDialog *self;
  g_autofree char *display_path = NULL;
  g_autofree char *file_text = NULL;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  self = g_object_new (PALANG_TYPE_COMPLAINT_DIALOG, NULL);
  self->file = g_object_ref (file);
  self->context = g_strdup (context);

  if (context != NULL)
    {
      g_autofree char *context_text = g_strdup_printf ("About %s", context);

      gtk_label_set_text (self->context_label, context_text);
      gtk_widget_set_visible (GTK_WIDGET (self->context_label), TRUE);
    }

  display_path = dup_display_path (file);
  file_text = g_strdup_printf ("Goes to %s", display_path);
  gtk_label_set_text (self->file_label, file_text);
  gtk_widget_set_tooltip_text (GTK_WIDGET (self->file_label), display_path);

  return self;
}
