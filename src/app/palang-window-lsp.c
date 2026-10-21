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
 * longer, and may hold a command to copy). The action win.restart-language-servers starts them all again,
 * e.g. after installing a missing server.
 */

#include "config.h"

#include "palang-window-private.h"

static PalangEditor *
editor_for_page (AdwTabPage *page)
{
  return PALANG_EDITOR (adw_tab_page_get_child (page));
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

  palang_lsp_manager_add_document (self->lsp_manager, palang_editor_get_document (editor));
  palang_lsp_manager_add_view (self->lsp_manager, palang_editor_get_view (editor));
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
  AdwToast *toast = adw_toast_new (message);

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
