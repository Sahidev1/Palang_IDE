/* palang-window-lsp.c — PalangWindow's language servers.
 *
 * Part of PalangWindow (see palang-window-private.h for how the window's
 * code is split). The window owns one PalangLspManager (lsp/) and keeps it
 * informed:
 *
 *   - the project folder: bound to the file tree's root
 *   - the open documents: every tab added to or removed from the tab view,
 *     and its view (for the diagnostics' gutter icons and hover popups)
 *
 * The manager starts the servers those documents need. When one can't be
 * started (not installed, keeps crashing), its message is shown as a
 * toast with a "Details" button (a toast has one line; the message can be
 * longer, and may hold a command to copy). The action
 * win.restart-language-servers starts them all again, e.g. after
 * installing a missing server.
 *
 * Go to definition: win.go-to-definition (F12) for the name at the
 * cursor, or Ctrl+click on a name. The server says where it is defined,
 * and the window opens that file there.
 */

#include "config.h"

#include <string.h>

#include "palang-window-private.h"

static PalangEditor *
editor_for_page (AdwTabPage *page)
{
  return PALANG_EDITOR (adw_tab_page_get_child (page));
}

/* ---- Go to definition --------------------------------------------------- */

static void
on_definition_found (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr (PalangWindow) self = user_data;  /* reference taken in go_to_definition */
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) error = NULL;
  guint line, column;

  if (!palang_lsp_manager_find_definition_finish (PALANG_LSP_MANAGER (source), result,
                                                  &file, &line, &column, &error))
    {
      /* Cancelled: the text changed meanwhile; the user has moved on. */
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        _palang_window_show_toast (self, error->message);
      return;
    }

  if (file == NULL)
    {
      _palang_window_show_toast (self, "No definition found");
      return;
    }

  /* The window counts lines and columns from 1. */
  palang_window_open_file_at (self, file, line + 1, column + 1);
}

/* Asks where the name at the cursor of @editor is defined. */
static void
go_to_definition (PalangWindow *self,
                  PalangEditor *editor)
{
  PalangDocument *document = palang_editor_get_document (editor);
  PalangLspClient *client = palang_lsp_manager_get_client (self->lsp_manager, document);
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (document);
  GtkTextIter cursor;

  if (client == NULL)
    {
      _palang_window_show_toast (self, "No language server for this file");
      return;
    }
  if (!palang_lsp_client_is_ready (client))
    {
      g_autofree char *message =
        g_strdup_printf ("The %s language server isn't running yet",
                         palang_lsp_client_get_title (client));

      _palang_window_show_toast (self, message);
      return;
    }

  gtk_text_buffer_get_iter_at_mark (buffer, &cursor, gtk_text_buffer_get_insert (buffer));
  palang_lsp_manager_find_definition_async (self->lsp_manager, document, &cursor, NULL,
                                            on_definition_found, g_object_ref (self));
}

static void
go_to_definition_action (GtkWidget  *widget,
                         const char *action_name,
                         GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  PalangEditor *editor = palang_window_get_active_editor (self);

  if (editor != NULL)
    go_to_definition (self, editor);
}

/* A click in an editor's text. With Ctrl held, it puts the cursor where
 * the click was and goes to the definition of the name there. The
 * gesture is "claimed", so the text view doesn't also start a selection;
 * a click without Ctrl is "denied" and handled by the view as usual. */
static void
on_text_pressed (GtkGestureClick *gesture,
                 int              n_press,
                 double           x,
                 double           y,
                 PalangWindow    *self)
{
  GtkWidget *view = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));
  PalangEditor *editor = PALANG_EDITOR (gtk_widget_get_ancestor (view, PALANG_TYPE_EDITOR));
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  GtkTextIter iter;
  int buffer_x, buffer_y;

  if ((state & GDK_CONTROL_MASK) == 0 || n_press != 1 || editor == NULL)
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
      return;
    }

  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

  /* Widget coordinates → buffer coordinates (they differ once scrolled) →
   * the position in the text. */
  gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (view), GTK_TEXT_WINDOW_WIDGET,
                                         (int) x, (int) y, &buffer_x, &buffer_y);
  gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (view), &iter, buffer_x, buffer_y);
  gtk_text_buffer_place_cursor (buffer, &iter);

  go_to_definition (self, editor);
}

/* "page-attached" and "page-detached" are AdwTabView's signals for a tab
 * being added and removed, however it happens (opened, restored, closed). */
static void
on_page_attached (AdwTabView   *tab_view,
                  AdwTabPage   *page,
                  int           position,
                  PalangWindow *self)
{
  PalangEditor *editor = editor_for_page (page);

  GtkGesture *click;

  palang_lsp_manager_add_document (self->lsp_manager, palang_editor_get_document (editor));
  palang_lsp_manager_add_view (self->lsp_manager, palang_editor_get_view (editor));

  /* Ctrl+click: in the "capture" phase, so it sees the click before the
   * text view's own click handling. */
  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_PRIMARY);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click), GTK_PHASE_CAPTURE);
  g_signal_connect (click, "pressed", G_CALLBACK (on_text_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (palang_editor_get_view (editor)), GTK_EVENT_CONTROLLER (click));
}

static void
on_page_detached (AdwTabView   *tab_view,
                  AdwTabPage   *page,
                  int           position,
                  PalangWindow *self)
{
  palang_lsp_manager_remove_document (self->lsp_manager,
                                      palang_editor_get_document (editor_for_page (page)));
}

/* The toast's "Details" button: the whole message in a dialog. The text
 * is a selectable label, so an install command can be copied from it. */
static void
on_details_clicked (AdwToast     *toast,
                    PalangWindow *self)
{
  const char *message = g_object_get_data (G_OBJECT (toast), "message");
  AdwDialog *dialog = adw_alert_dialog_new ("Language Server Problem", NULL);
  GtkWidget *label = gtk_label_new (message);

  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), label);
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", "Close");
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
on_server_failed (PalangLspManager *manager,
                  const char       *message,
                  PalangWindow     *self)
{
  /* The toast shows the first line; "Details" shows all of it (an install
   * hint can add more lines). */
  g_autofree char *first_line = g_strndup (message, strcspn (message, "\n"));
  AdwToast *toast = adw_toast_new (first_line);

  /* Toast titles are Pango markup by default; a server's message is plain
   * text and may contain "<" or "&". */
  adw_toast_set_use_markup (toast, FALSE);
  adw_toast_set_timeout (toast, 10);
  adw_toast_set_button_label (toast, "Details");
  /* g_object_set_data_full () attaches a value to any GObject by name; the
   * copy of the message is freed (g_free) when the toast goes away. The
   * toast lives inside the window, so it can't outlive @self. */
  g_object_set_data_full (G_OBJECT (toast), "message", g_strdup (message), g_free);
  g_signal_connect (toast, "button-clicked", G_CALLBACK (on_details_clicked), self);
  adw_toast_overlay_add_toast (self->toast_overlay, toast);
}

static void
restart_language_servers_action (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *parameter)
{
  palang_lsp_manager_restart (PALANG_WINDOW (widget)->lsp_manager);
}

/* ---- Setup, called from palang-window.c --------------------------------- */

void
_palang_window_class_init_lsp (GtkWidgetClass *widget_class)
{
  gtk_widget_class_install_action (widget_class, "win.restart-language-servers", NULL,
                                   restart_language_servers_action);
  gtk_widget_class_install_action (widget_class, "win.go-to-definition", NULL,
                                   go_to_definition_action);
}

void
_palang_window_init_lsp (PalangWindow *self)
{
  self->lsp_manager = palang_lsp_manager_new (palang_settings_get_default ());

  g_object_bind_property (self->file_tree, "root", self->lsp_manager, "folder",
                          G_BINDING_SYNC_CREATE);
  g_signal_connect (self->lsp_manager, "server-failed", G_CALLBACK (on_server_failed), self);
  g_signal_connect (self->tab_view, "page-attached", G_CALLBACK (on_page_attached), self);
  g_signal_connect (self->tab_view, "page-detached", G_CALLBACK (on_page_detached), self);
}

/* Called from dispose, while the tab view still exists. Only our own two
 * handlers are disconnected; palang-window.c has others on the tab view.
 * The manager asks its servers to exit; they finish that on their own
 * (holding the application until they have). */
void
_palang_window_dispose_lsp (PalangWindow *self)
{
  if (self->tab_view != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->tab_view, on_page_attached, self);
      g_signal_handlers_disconnect_by_func (self->tab_view, on_page_detached, self);
    }
  g_clear_object (&self->lsp_manager);
}
