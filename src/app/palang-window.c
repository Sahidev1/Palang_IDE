/* palang-window.c — the main window: tabs, file actions, close prompts.
 *
 * Layout is in data/ui/palang-window.ui. Each tab (an AdwTabPage in the
 * AdwTabView) holds one PalangEditor, which holds one PalangDocument.
 *
 * Under the tabs is the bottom panel (PalangPanel, palang-panel.c) in a
 * vertical GtkPaned; Ctrl+J shows and hides it.
 *
 * The window's code is split over several files: the bottom panel is in
 * palang-window-panel.c, the command palette and Quick Open in
 * palang-window-palette.c. palang-window-private.h has the struct they
 * share and explains the split.
 *
 * Sections of this file:
 *   - Tabs: adding, finding, and keeping the window title in sync
 *   - Opening files and folders
 *   - Saving (Save, Save As, Save All)
 *   - Closing a tab with unsaved changes
 *   - Closing the window with unsaved changes
 *   - Session: remembering what was open
 *   - Actions (what menu items and shortcuts call)
 */

#include "config.h"

#include "palang-save-flow.h"
#include "palang-window-private.h"


G_DEFINE_FINAL_TYPE (PalangWindow, palang_window, ADW_TYPE_APPLICATION_WINDOW)


enum {
  PROP_0,
  PROP_ACTIVE_EDITOR,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

void
_palang_window_show_toast (PalangWindow *self,
                           const char   *message)
{
  adw_toast_overlay_add_toast (self->toast_overlay, adw_toast_new (message));
}

/* Shows an error from a save, unless the user simply cancelled a dialog. */
static void
show_save_error (PalangWindow   *self,
                 PalangDocument *document,
                 const GError   *error)
{
  g_autofree char *title = NULL;
  g_autofree char *message = NULL;

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  title = palang_document_dup_title (document);
  message = g_strdup_printf ("Could not save “%s”: %s", title, error->message);
  _palang_window_show_toast (self, message);
}

static PalangEditor *
editor_for_page (AdwTabPage *page)
{
  return PALANG_EDITOR (adw_tab_page_get_child (page));
}

/* ---- Tabs --------------------------------------------------------------- */

/* Turns "main.c" into "main.c — Palang" for the taskbar. */
static gboolean
add_app_name (GBinding     *binding,
              const GValue *from_value,
              GValue       *to_value,
              gpointer      user_data)
{
  g_value_take_string (to_value,
                       g_strdup_printf ("%s — Palang", g_value_get_string (from_value)));
  return TRUE;
}

static void
clear_binding (GBinding **binding)
{
  if (*binding == NULL)
    return;

  g_binding_unbind (*binding);
  g_clear_object (binding);
}

/* Another tab was selected (or the last one closed). The header bar and
 * taskbar show the selected editor's title (and path), and whoever watches
 * "active-editor" is told. */
static void
on_selected_page_changed (AdwTabView   *tab_view,
                          GParamSpec   *pspec,
                          PalangWindow *self)
{
  AdwTabPage *page = adw_tab_view_get_selected_page (tab_view);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_EDITOR]);

  clear_binding (&self->title_binding);
  clear_binding (&self->subtitle_binding);
  clear_binding (&self->window_title_binding);

  if (page == NULL)
    {
      adw_window_title_set_title (self->window_title, "Palang");
      adw_window_title_set_subtitle (self->window_title, NULL);
      gtk_window_set_title (GTK_WINDOW (self), "Palang");
      return;
    }

  /* SYNC_CREATE copies the current value right away; afterwards GObject
   * copies it whenever the editor's property changes. */
  self->title_binding =
    g_object_ref (g_object_bind_property (editor_for_page (page), "title",
                                          self->window_title, "title",
                                          G_BINDING_SYNC_CREATE));
  self->subtitle_binding =
    g_object_ref (g_object_bind_property (editor_for_page (page), "path",
                                          self->window_title, "subtitle",
                                          G_BINDING_SYNC_CREATE));
  self->window_title_binding =
    g_object_ref (g_object_bind_property_full (editor_for_page (page), "title",
                                               self, "title",
                                               G_BINDING_SYNC_CREATE,
                                               add_app_name, NULL, NULL, NULL));
}

/* Enables/disables actions and switches between the "No Open Files" page
 * and the tabs. */
static void
on_n_pages_changed (AdwTabView   *tab_view,
                    GParamSpec   *pspec,
                    PalangWindow *self)
{
  gboolean has_tabs = adw_tab_view_get_n_pages (tab_view) > 0;

  gtk_stack_set_visible_child_name (self->stack, has_tabs ? "tabs" : "empty");
  gtk_widget_set_visible (GTK_WIDGET (self->tab_bar), has_tabs);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.save", has_tabs);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.save-as", has_tabs);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.save-all", has_tabs);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.close-tab", has_tabs);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.find", has_tabs);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.replace", has_tabs);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.find-next", has_tabs);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.find-previous", has_tabs);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.go-to-line", has_tabs);
}

/* Adds a tab for @document, selects it and focuses its text. */
static AdwTabPage *
add_editor (PalangWindow   *self,
            PalangDocument *document)
{
  PalangEditor *editor = palang_editor_new (document);
  AdwTabPage *page = adw_tab_view_append (self->tab_view, GTK_WIDGET (editor));

  /* The tab label and tooltip follow the editor's title and path. */
  g_object_bind_property (editor, "title", page, "title", G_BINDING_SYNC_CREATE);
  g_object_bind_property (editor, "path", page, "tooltip", G_BINDING_SYNC_CREATE);

  adw_tab_view_set_selected_page (self->tab_view, page);
  gtk_widget_grab_focus (GTK_WIDGET (editor));

  return page;
}

/* Returns: (transfer none) (nullable): the tab showing @file, if any. */
static AdwTabPage *
find_page_for_file (PalangWindow *self,
                    GFile        *file)
{
  guint n_pages = adw_tab_view_get_n_pages (self->tab_view);

  for (guint i = 0; i < n_pages; i++)
    {
      AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, i);
      GFile *page_file = palang_document_get_file (palang_editor_get_document (editor_for_page (page)));

      if (page_file != NULL && g_file_equal (page_file, file))
        return page;
    }

  return NULL;
}

/* A never-saved, untouched, empty document: opening a file replaces it
 * instead of leaving a useless "Untitled" tab behind. */
static gboolean
is_blank_untitled (PalangEditor *editor)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (palang_editor_get_document (editor));

  return palang_document_get_file (PALANG_DOCUMENT (buffer)) == NULL &&
         !gtk_text_buffer_get_modified (buffer) &&
         gtk_text_buffer_get_char_count (buffer) == 0;
}

/* Returns: (transfer container): the documents with unsaved changes. Free
 * the array with g_ptr_array_unref(); the documents aren't owned by it. */
static GPtrArray *
collect_modified_documents (PalangWindow *self)
{
  GPtrArray *documents = g_ptr_array_new ();
  guint n_pages = adw_tab_view_get_n_pages (self->tab_view);

  for (guint i = 0; i < n_pages; i++)
    {
      AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, i);
      PalangDocument *document = palang_editor_get_document (editor_for_page (page));

      if (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (document)))
        g_ptr_array_add (documents, document);
    }

  return documents;
}

PalangEditor *
palang_window_get_active_editor (PalangWindow *self)
{
  AdwTabPage *page;

  g_return_val_if_fail (PALANG_IS_WINDOW (self), NULL);

  page = adw_tab_view_get_selected_page (self->tab_view);
  return page != NULL ? editor_for_page (page) : NULL;
}

guint
palang_window_get_n_editors (PalangWindow *self)
{
  g_return_val_if_fail (PALANG_IS_WINDOW (self), 0);

  return adw_tab_view_get_n_pages (self->tab_view);
}

void
palang_window_new_document (PalangWindow *self)
{
  g_autoptr (PalangDocument) document = palang_document_new ();

  g_return_if_fail (PALANG_IS_WINDOW (self));

  add_editor (self, document);
}

/* ---- Opening files ------------------------------------------------------ */

/* What to do once a file has loaded: which window, and where to put the
 * cursor ("12:5", or NULL to leave it at the start). */
typedef struct {
  PalangWindow *window;    /* owned */
  char         *position;  /* owned, nullable */
} OpenData;

static void
open_data_free (OpenData *data)
{
  g_clear_object (&data->window);
  g_free (data->position);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (OpenData, open_data_free)

static void
on_file_loaded (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
  PalangDocument *document = PALANG_DOCUMENT (source);
  g_autoptr (OpenData) data = user_data;  /* created in open_file_at */
  PalangWindow *self = data->window;
  g_autoptr (GError) error = NULL;
  PalangEditor *active;

  if (!palang_document_load_finish (document, result, &error))
    {
      g_autofree char *message = g_strdup_printf ("Could not open file: %s", error->message);

      if (self->toast_overlay != NULL)
        _palang_window_show_toast (self, message);
      return;
    }

  /* The window may have been closed while the file was loading. */
  if (self->tab_view == NULL)
    return;

  active = palang_window_get_active_editor (self);
  if (active != NULL && is_blank_untitled (active))
    adw_tab_view_close_page (self->tab_view,
                             adw_tab_view_get_selected_page (self->tab_view));

  add_editor (self, document);
  if (data->position != NULL)
    palang_editor_go_to (palang_window_get_active_editor (self), data->position);
}

/* @position: (nullable): "line:column" for palang_editor_go_to (). */
static void
open_file_at (PalangWindow *self,
              GFile        *file,
              const char   *position)
{
  g_autoptr (PalangDocument) document = NULL;
  AdwTabPage *existing;
  OpenData *data;

  existing = find_page_for_file (self, file);
  if (existing != NULL)
    {
      adw_tab_view_set_selected_page (self->tab_view, existing);
      if (position != NULL)
        {
          palang_editor_go_to (editor_for_page (existing), position);
          gtk_widget_grab_focus (GTK_WIDGET (editor_for_page (existing)));
        }
      return;
    }

  /* The load operation keeps the document alive until on_file_loaded; we
   * pass a reference to the window along so it can't disappear meanwhile. */
  data = g_new0 (OpenData, 1);
  data->window = g_object_ref (self);
  data->position = g_strdup (position);

  document = palang_document_new ();
  palang_document_load_async (document, file, NULL, on_file_loaded, data);
}

void
palang_window_open_file (PalangWindow *self,
                         GFile        *file)
{
  g_return_if_fail (PALANG_IS_WINDOW (self));
  g_return_if_fail (G_IS_FILE (file));

  open_file_at (self, file, NULL);
}

void
palang_window_open_file_at (PalangWindow *self,
                            GFile        *file,
                            guint         line,
                            guint         column)
{
  g_autofree char *position = NULL;

  g_return_if_fail (PALANG_IS_WINDOW (self));
  g_return_if_fail (G_IS_FILE (file));

  position = g_strdup_printf ("%u:%u", line, column);
  open_file_at (self, file, position);
}

static void
on_open_dialog_done (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr (PalangWindow) self = user_data;
  g_autoptr (GListModel) files = NULL;
  guint n_files;

  /* NULL when the user cancelled the dialog; nothing to do then. */
  files = gtk_file_dialog_open_multiple_finish (GTK_FILE_DIALOG (source), result, NULL);
  if (files == NULL)
    return;

  n_files = g_list_model_get_n_items (files);
  for (guint i = 0; i < n_files; i++)
    {
      g_autoptr (GFile) file = g_list_model_get_item (files, i);
      palang_window_open_file (self, file);
    }
}

void
palang_window_open_folder (PalangWindow *self,
                           GFile        *folder)
{
  g_autofree char *name = NULL;

  g_return_if_fail (PALANG_IS_WINDOW (self));
  g_return_if_fail (G_IS_FILE (folder));

  palang_file_tree_set_root (self->file_tree, folder);

  name = g_file_get_basename (folder);
  adw_window_title_set_title (self->sidebar_title, name);
  adw_overlay_split_view_set_show_sidebar (self->split_view, TRUE);
}

GFile *
palang_window_get_folder (PalangWindow *self)
{
  g_return_val_if_fail (PALANG_IS_WINDOW (self), NULL);

  return palang_file_tree_get_root (self->file_tree);
}

static void
on_open_folder_dialog_done (GObject      *source,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr (PalangWindow) self = user_data;
  g_autoptr (GFile) folder = NULL;

  /* NULL when the user cancelled the dialog. */
  folder = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source), result, NULL);
  if (folder != NULL && self->file_tree != NULL)
    palang_window_open_folder (self, folder);
}

/* A file was clicked in the folder tree (connected in the .ui file). */
static void
on_file_activated (PalangFileTree *file_tree,
                   GFile          *file,
                   PalangWindow   *self)
{
  palang_window_open_file (self, file);
}

/* ---- Saving ------------------------------------------------------------- */

static void
on_save_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  PalangDocument *document = PALANG_DOCUMENT (source);
  g_autoptr (PalangWindow) self = user_data;
  g_autoptr (GError) error = NULL;

  if (!palang_save_document_finish (document, result, &error) && self->toast_overlay != NULL)
    show_save_error (self, document, error);
}

static void
save_active (PalangWindow *self,
             gboolean      ask_location)
{
  PalangEditor *editor = palang_window_get_active_editor (self);

  if (editor == NULL)
    return;

  palang_save_document_async (GTK_WINDOW (self), palang_editor_get_document (editor),
                              ask_location, NULL, on_save_done, g_object_ref (self));
}

/* Saving several documents, one after another (Save All, and "Save All"
 * when closing the window). Stops at the first failure or cancel, so the
 * user isn't shown a pile of dialogs after pressing Cancel once. */

typedef struct {
  PalangWindow *window;     /* owned */
  GPtrArray    *documents;  /* owns a reference to each document */
  guint         next;       /* index of the next document to save */
} SaveAllData;

static void
save_all_data_free (SaveAllData *data)
{
  g_clear_object (&data->window);
  g_clear_pointer (&data->documents, g_ptr_array_unref);
  g_free (data);
}

static void save_next (GTask *task);

static void
on_one_saved (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  GTask *task = user_data;
  GError *error = NULL;

  if (!palang_save_document_finish (PALANG_DOCUMENT (source), result, &error))
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  save_next (task);
}

static void
save_next (GTask *task)
{
  SaveAllData *data = g_task_get_task_data (task);

  if (data->next >= data->documents->len)
    {
      g_task_return_boolean (task, TRUE);
      g_object_unref (task);
      return;
    }

  palang_save_document_async (GTK_WINDOW (data->window),
                              g_ptr_array_index (data->documents, data->next++),
                              FALSE, NULL, on_one_saved, task);
}

/* Saves @documents in order. Finishes with TRUE if all were saved. */
static void
save_documents_async (PalangWindow        *self,
                      GPtrArray           *documents,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  GTask *task = g_task_new (self, NULL, callback, user_data);
  SaveAllData *data = g_new0 (SaveAllData, 1);

  data->window = g_object_ref (self);
  data->documents = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < documents->len; i++)
    g_ptr_array_add (data->documents, g_object_ref (g_ptr_array_index (documents, i)));

  g_task_set_task_data (task, data, (GDestroyNotify) save_all_data_free);
  save_next (task);
}

static gboolean
save_documents_finish (PalangWindow  *self,
                       GAsyncResult  *result,
                       GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
on_save_all_done (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  PalangWindow *self = PALANG_WINDOW (source);
  g_autoptr (GError) error = NULL;

  if (!save_documents_finish (self, result, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    _palang_window_show_toast (self, error->message);
}

/* ---- Closing a tab with unsaved changes --------------------------------- */

/* Context for one "close this tab?" prompt. */
typedef struct {
  PalangWindow *window;  /* owned */
  AdwTabPage   *page;    /* owned */
} ClosePageData;

static void
close_page_data_free (ClosePageData *data)
{
  g_clear_object (&data->window);
  g_clear_object (&data->page);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClosePageData, close_page_data_free)

/* Tells the tab view whether to really close the page. */
static void
finish_close_page (ClosePageData *data,
                   gboolean       close)
{
  if (data->window->tab_view != NULL)
    adw_tab_view_close_page_finish (data->window->tab_view, data->page, close);
}

static void
on_close_page_saved (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr (ClosePageData) data = user_data;
  g_autoptr (GError) error = NULL;
  gboolean saved = palang_save_document_finish (PALANG_DOCUMENT (source), result, &error);

  if (!saved && data->window->toast_overlay != NULL)
    show_save_error (data->window, PALANG_DOCUMENT (source), error);

  finish_close_page (data, saved);
}

static void
on_close_page_response (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr (ClosePageData) data = user_data;
  const char *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (source), result);
  PalangDocument *document = palang_editor_get_document (editor_for_page (data->page));

  if (g_strcmp0 (response, "save") == 0)
    {
      /* Read data->window *before* the call: C doesn't define the order in
       * which arguments are evaluated, so g_steal_pointer (&data) (which sets
       * data to NULL) might run first. */
      GtkWindow *parent = GTK_WINDOW (data->window);

      /* The save decides; ownership of `data` moves to on_close_page_saved. */
      palang_save_document_async (parent, document, FALSE, NULL,
                                  on_close_page_saved, g_steal_pointer (&data));
      return;
    }

  finish_close_page (data, g_strcmp0 (response, "discard") == 0);
}

/* AdwTabView emits "close-page" when a tab is about to close (close button,
 * Ctrl+W, …). Returning GDK_EVENT_STOP means "we'll decide", and we must
 * then call adw_tab_view_close_page_finish() — possibly later, after asking
 * the user. */
static gboolean
on_close_page (AdwTabView   *tab_view,
               AdwTabPage   *page,
               PalangWindow *self)
{
  PalangDocument *document = palang_editor_get_document (editor_for_page (page));
  g_autofree char *title = NULL;
  ClosePageData *data;
  AdwDialog *dialog;

  if (!gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (document)))
    {
      adw_tab_view_close_page_finish (tab_view, page, TRUE);
      return GDK_EVENT_STOP;
    }

  /* Show the tab the question is about. */
  adw_tab_view_set_selected_page (tab_view, page);

  title = palang_document_dup_title (document);
  dialog = adw_alert_dialog_new ("Save Changes?", NULL);
  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                                "“%s” has unsaved changes. Changes which are not "
                                "saved will be lost.",
                                title);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", "_Cancel",
                                  "discard", "_Discard",
                                  "save", "_Save",
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "discard",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "save",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "save");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  data = g_new0 (ClosePageData, 1);
  data->window = g_object_ref (self);
  data->page = g_object_ref (page);

  adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (self), NULL,
                           on_close_page_response, data);

  return GDK_EVENT_STOP;
}

/* ---- Closing the window with unsaved changes ---------------------------- */

static void save_session (PalangWindow *self);

/* The user has decided (discard or saved everything): close for real.
 *
 * We destroy the window directly instead of calling gtk_window_close()
 * again. That would emit "close-request" once more, and libadwaita swallows
 * close requests while one of its dialogs is still finishing its closing
 * animation — so after a quick "Save All" the window would stay open. */
static void
close_without_asking (PalangWindow *self)
{
  save_session (self);
  gtk_window_destroy (GTK_WINDOW (self));
}

static void
on_close_window_saved (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  PalangWindow *self = PALANG_WINDOW (source);
  g_autoptr (GError) error = NULL;

  if (save_documents_finish (self, result, &error))
    close_without_asking (self);
  else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    _palang_window_show_toast (self, error->message);
}

static void
on_close_window_response (GObject      *source,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr (PalangWindow) self = user_data;
  const char *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (source), result);

  if (g_strcmp0 (response, "discard") == 0)
    {
      close_without_asking (self);
    }
  else if (g_strcmp0 (response, "save") == 0)
    {
      g_autoptr (GPtrArray) documents = collect_modified_documents (self);
      save_documents_async (self, documents, on_close_window_saved, NULL);
    }
}

/* Called when the user closes the window (X button, Ctrl+Q). Returning TRUE
 * keeps the window open. */
static gboolean
palang_window_close_request (GtkWindow *window)
{
  PalangWindow *self = PALANG_WINDOW (window);
  g_autoptr (GPtrArray) documents = NULL;
  g_autofree char *body = NULL;
  AdwDialog *dialog;

  documents = collect_modified_documents (self);
  if (documents->len == 0)
    {
      save_session (self);
      return GTK_WINDOW_CLASS (palang_window_parent_class)->close_request (window);
    }

  if (documents->len == 1)
    {
      g_autofree char *title = palang_document_dup_title (g_ptr_array_index (documents, 0));
      body = g_strdup_printf ("“%s” has unsaved changes.", title);
    }
  else
    {
      body = g_strdup_printf ("%u open documents have unsaved changes.", documents->len);
    }

  dialog = adw_alert_dialog_new ("Save Changes Before Closing?", body);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", "_Cancel",
                                  "discard", "_Discard All",
                                  "save", "_Save All",
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "discard",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "save",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "save");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (self), NULL,
                           on_close_window_response, g_object_ref (self));

  return TRUE;
}

/* ---- Session: remembering what was open --------------------------------- */

PalangSession *
palang_window_dup_session (PalangWindow *self)
{
  PalangSession *session = palang_session_new ();
  AdwTabPage *selected;
  guint n_pages;

  g_return_val_if_fail (PALANG_IS_WINDOW (self), session);

  if (palang_window_get_folder (self) != NULL)
    session->folder = g_object_ref (palang_window_get_folder (self));
  session->show_sidebar = adw_overlay_split_view_get_show_sidebar (self->split_view);
  session->show_panel = gtk_widget_get_visible (GTK_WIDGET (self->panel));
  session->panel_height = _palang_window_get_panel_height (self);
  session->panel_page = g_strdup (palang_panel_get_visible_page (self->panel));

  selected = adw_tab_view_get_selected_page (self->tab_view);
  n_pages = adw_tab_view_get_n_pages (self->tab_view);

  for (guint i = 0; i < n_pages; i++)
    {
      AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, i);
      GFile *file = palang_document_get_file (palang_editor_get_document (editor_for_page (page)));

      if (file == NULL)
        continue;

      if (page == selected)
        session->active = session->files->len;
      g_ptr_array_add (session->files, g_object_ref (file));
    }

  return session;
}

/* Writes the session file; called just before the window closes. */
static void
save_session (PalangWindow *self)
{
  g_autoptr (PalangSession) session = palang_window_dup_session (self);
  g_autofree char *path = palang_session_default_path ();
  g_autoptr (GError) error = NULL;

  if (!palang_session_save (session, path, &error))
    g_warning ("Could not save the session to %s: %s", path, error->message);
}

/* Restoring opens the files one after another, so the tabs come back in
 * their original order (loading them all at once would add each tab
 * whenever its file happened to finish loading). */
typedef struct {
  PalangWindow *window;  /* owned */
  GPtrArray    *files;   /* owned; GFile */
  guint         next;    /* index of the file being loaded */
  int           active;  /* index of the tab to select at the end */
  AdwTabPage   *active_page;  /* not owned; set once that file is open */
} RestoreData;

static void
restore_data_free (RestoreData *data)
{
  g_clear_object (&data->window);
  g_clear_pointer (&data->files, g_ptr_array_unref);
  g_free (data);
}

static void restore_next_file (RestoreData *data);

static void
on_restored_file_loaded (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  RestoreData *data = user_data;
  PalangDocument *document = PALANG_DOCUMENT (source);

  /* A file that can't be opened (deleted since) is skipped quietly. */
  if (palang_document_load_finish (document, result, NULL) && data->window->tab_view != NULL)
    {
      AdwTabPage *page = add_editor (data->window, document);

      if ((int) data->next == data->active)
        data->active_page = page;
    }

  g_object_unref (document);
  data->next++;
  restore_next_file (data);
}

static void
restore_next_file (RestoreData *data)
{
  if (data->next >= data->files->len || data->window->tab_view == NULL)
    {
      if (data->active_page != NULL && data->window->tab_view != NULL)
        adw_tab_view_set_selected_page (data->window->tab_view, data->active_page);
      restore_data_free (data);
      return;
    }

  /* The document's reference is released in on_restored_file_loaded. */
  palang_document_load_async (palang_document_new (),
                              g_ptr_array_index (data->files, data->next),
                              NULL, on_restored_file_loaded, data);
}

void
palang_window_restore_session (PalangWindow        *self,
                               const PalangSession *session)
{
  RestoreData *data;

  g_return_if_fail (PALANG_IS_WINDOW (self));
  g_return_if_fail (session != NULL);

  if (session->folder != NULL && g_file_query_exists (session->folder, NULL))
    palang_window_open_folder (self, session->folder);
  adw_overlay_split_view_set_show_sidebar (self->split_view,
                                           session->show_sidebar &&
                                           palang_window_get_folder (self) != NULL);

  /* Showing the panel applies the height (on_panel_visible_changed). */
  if (session->panel_height > 0)
    self->panel_height = session->panel_height;
  if (session->panel_page != NULL)
    palang_panel_set_visible_page (self->panel, session->panel_page);
  gtk_widget_set_visible (GTK_WIDGET (self->panel), session->show_panel);

  data = g_new0 (RestoreData, 1);
  data->window = g_object_ref (self);
  data->files = g_ptr_array_ref (session->files);
  data->active = session->active;
  restore_next_file (data);
}

/* ---- Actions ------------------------------------------------------------ */

/* Actions installed with gtk_widget_class_install_action() are called with
 * the widget, the action's name and its parameter (unused here). */

static void
new_action (GtkWidget  *widget,
            const char *action_name,
            GVariant   *parameter)
{
  palang_window_new_document (PALANG_WINDOW (widget));
}

static void
open_action (GtkWidget  *widget,
             const char *action_name,
             GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
  PalangEditor *active = palang_window_get_active_editor (self);

  gtk_file_dialog_set_title (dialog, "Open Files");

  /* Start in the folder of the current file, or else the open folder. */
  if (active != NULL && palang_document_get_file (palang_editor_get_document (active)) != NULL)
    {
      GFile *file = palang_document_get_file (palang_editor_get_document (active));
      g_autoptr (GFile) folder = g_file_get_parent (file);

      gtk_file_dialog_set_initial_folder (dialog, folder);
    }
  else if (palang_window_get_folder (self) != NULL)
    {
      gtk_file_dialog_set_initial_folder (dialog, palang_window_get_folder (self));
    }

  gtk_file_dialog_open_multiple (dialog, GTK_WINDOW (self), NULL,
                                 on_open_dialog_done, g_object_ref (self));
}

static void
open_folder_action (GtkWidget  *widget,
                    const char *action_name,
                    GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();

  gtk_file_dialog_set_title (dialog, "Open Folder");
  if (palang_window_get_folder (self) != NULL)
    gtk_file_dialog_set_initial_folder (dialog, palang_window_get_folder (self));

  gtk_file_dialog_select_folder (dialog, GTK_WINDOW (self), NULL,
                                 on_open_folder_dialog_done, g_object_ref (self));
}

static void
toggle_sidebar_action (GtkWidget  *widget,
                       const char *action_name,
                       GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);

  adw_overlay_split_view_set_show_sidebar (self->split_view,
                                           !adw_overlay_split_view_get_show_sidebar (self->split_view));
}

/* Find / Replace / Go to line: forwarded to the editor in the current tab. */

static void
find_action (GtkWidget  *widget,
             const char *action_name,
             GVariant   *parameter)
{
  PalangEditor *editor = palang_window_get_active_editor (PALANG_WINDOW (widget));

  if (editor != NULL)
    palang_editor_show_search (editor, g_str_equal (action_name, "win.replace"));
}

static void
find_next_action (GtkWidget  *widget,
                  const char *action_name,
                  GVariant   *parameter)
{
  PalangEditor *editor = palang_window_get_active_editor (PALANG_WINDOW (widget));

  if (editor != NULL)
    palang_editor_find_next (editor, g_str_equal (action_name, "win.find-next"));
}

static void
go_to_line_action (GtkWidget  *widget,
                   const char *action_name,
                   GVariant   *parameter)
{
  PalangEditor *editor = palang_window_get_active_editor (PALANG_WINDOW (widget));

  if (editor != NULL)
    palang_editor_show_go_to (editor);
}

static void
save_action (GtkWidget  *widget,
             const char *action_name,
             GVariant   *parameter)
{
  save_active (PALANG_WINDOW (widget), FALSE);
}

static void
save_as_action (GtkWidget  *widget,
                const char *action_name,
                GVariant   *parameter)
{
  save_active (PALANG_WINDOW (widget), TRUE);
}

static void
save_all_action (GtkWidget  *widget,
                 const char *action_name,
                 GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  g_autoptr (GPtrArray) documents = collect_modified_documents (self);

  if (documents->len > 0)
    save_documents_async (self, documents, on_save_all_done, NULL);
}

static void
close_tab_action (GtkWidget  *widget,
                  const char *action_name,
                  GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  AdwTabPage *page = adw_tab_view_get_selected_page (self->tab_view);

  /* This emits "close-page", which asks about unsaved changes. */
  if (page != NULL)
    adw_tab_view_close_page (self->tab_view, page);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_window_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PalangWindow *self = PALANG_WINDOW (object);

  switch (prop_id)
    {
    case PROP_ACTIVE_EDITOR:
      g_value_set_object (value, palang_window_get_active_editor (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_window_dispose (GObject *object)
{
  PalangWindow *self = PALANG_WINDOW (object);

  g_clear_object (&self->title_binding);
  g_clear_object (&self->subtitle_binding);
  g_clear_object (&self->window_title_binding);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_WINDOW);

  G_OBJECT_CLASS (palang_window_parent_class)->dispose (object);
}

static void
palang_window_class_init (PalangWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkWindowClass *window_class = GTK_WINDOW_CLASS (klass);

  object_class->get_property = palang_window_get_property;
  object_class->dispose = palang_window_dispose;
  window_class->close_request = palang_window_close_request;

  /* "active-editor": the PalangEditor in the selected tab, or NULL. It is
   * read-only; other parts of Palang listen to "notify::active-editor" to
   * follow the user from tab to tab. */
  properties[PROP_ACTIVE_EDITOR] =
    g_param_spec_object ("active-editor", NULL, NULL,
                         PALANG_TYPE_EDITOR,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  /* The .ui file uses <object class="PalangFileTree"> and "PalangPanel". */
  g_type_ensure (PALANG_TYPE_FILE_TREE);
  g_type_ensure (PALANG_TYPE_PANEL);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-window.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, split_view);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, sidebar_title);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, file_tree);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, window_title);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, tab_bar);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, stack);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, tab_view);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, paned);
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, panel);
  gtk_widget_class_bind_template_callback (widget_class, on_file_activated);

  /* Their names and keyboard shortcuts are in the command table in
   * palang-application.c. */
  gtk_widget_class_install_action (widget_class, "win.new", NULL, new_action);
  gtk_widget_class_install_action (widget_class, "win.open", NULL, open_action);
  gtk_widget_class_install_action (widget_class, "win.open-folder", NULL, open_folder_action);
  gtk_widget_class_install_action (widget_class, "win.toggle-sidebar", NULL, toggle_sidebar_action);
  gtk_widget_class_install_action (widget_class, "win.save", NULL, save_action);
  gtk_widget_class_install_action (widget_class, "win.save-as", NULL, save_as_action);
  gtk_widget_class_install_action (widget_class, "win.save-all", NULL, save_all_action);
  gtk_widget_class_install_action (widget_class, "win.close-tab", NULL, close_tab_action);
  /* find_action and find_next_action serve two actions each and look at
   * the action name to tell them apart. */
  gtk_widget_class_install_action (widget_class, "win.find", NULL, find_action);
  gtk_widget_class_install_action (widget_class, "win.replace", NULL, find_action);
  gtk_widget_class_install_action (widget_class, "win.find-next", NULL, find_next_action);
  gtk_widget_class_install_action (widget_class, "win.find-previous", NULL, find_next_action);
  gtk_widget_class_install_action (widget_class, "win.go-to-line", NULL, go_to_line_action);
  _palang_window_class_init_panel (widget_class);
  _palang_window_class_init_palette (widget_class);
}

static void
palang_window_init (PalangWindow *self)
{
  /* Gives keys to the terminal before the app's shortcuts see them; see
   * on_window_key_pressed in palang-window-panel.c. */
  _palang_window_init_key_routing (self);

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect (self->tab_view, "notify::selected-page",
                    G_CALLBACK (on_selected_page_changed), self);
  g_signal_connect (self->tab_view, "notify::n-pages",
                    G_CALLBACK (on_n_pages_changed), self);
  g_signal_connect (self->tab_view, "close-page",
                    G_CALLBACK (on_close_page), self);

  _palang_window_init_panel (self);

  /* Start in the "no tabs" state. */
  on_n_pages_changed (self->tab_view, NULL, self);
}

PalangWindow *
palang_window_new (PalangApplication *application)
{
  return g_object_new (PALANG_TYPE_WINDOW,
                       "application", application,
                       NULL);
}
