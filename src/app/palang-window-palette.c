/* palang-window-palette.c — PalangWindow's command palette and Quick Open.
 *
 * Part of PalangWindow (see palang-window-private.h for how the window's
 * code is split). Both show a PalangPalette (palette/) dialog:
 *
 *   - Ctrl+Shift+P, win.command-palette: the app's command table; the
 *     chosen command's action is run
 *   - Ctrl+P, win.quick-open: the project folder's files (listed by
 *     ripgrep); the chosen file is opened
 *
 * Only one palette is open at a time.
 */

#include "config.h"

#include "palang-command.h"
#include "palang-window-private.h"

static void
on_command_chosen (PalangPalette     *palette,
                   PalangPaletteItem *item,
                   PalangWindow      *self)
{
  /* The item's id is the action's name ("win.save-all", "app.quit");
   * actions are found by name from the window, "app." ones too. */
  gtk_widget_activate_action (GTK_WIDGET (self), palang_palette_item_get_id (item), NULL);
}

/* Only one palette at a time. Pressing Ctrl+Shift+P (or Ctrl+P) while its
 * palette is open keeps it as it is; the other shortcut switches to the
 * other palette. Returns: TRUE if the palette of @action is already open. */
static gboolean
palette_is_open (PalangWindow *self,
                 const char   *action)
{
  if (self->palette == NULL)
    return FALSE;

  if (g_str_equal (self->palette_action, action))
    return TRUE;

  adw_dialog_close (ADW_DIALOG (self->palette));
  return FALSE;
}

/* "closed" may come after a new palette was shown (the old one fades
 * out), so forget only the palette that actually closed. */
static void
on_palette_closed (AdwDialog    *dialog,
                   PalangWindow *self)
{
  if (ADW_DIALOG (self->palette) == dialog)
    self->palette = NULL;
}

static void
show_palette (PalangWindow  *self,
              PalangPalette *palette,
              const char    *action)
{
  self->palette = palette;
  self->palette_action = action;
  g_signal_connect (palette, "closed", G_CALLBACK (on_palette_closed), self);
  adw_dialog_present (ADW_DIALOG (palette), GTK_WIDGET (self));
}

/* Ctrl+Shift+P: every command of the app's command table, with its
 * shortcut as the subtitle. */
static void
command_palette_action (GtkWidget  *widget,
                        const char *action_name,
                        GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  g_autoptr (GListStore) items = g_list_store_new (PALANG_TYPE_PALETTE_ITEM);
  GListModel *commands;
  PalangPalette *palette;

  if (app == NULL || palette_is_open (self, "win.command-palette"))
    return;

  commands = palang_application_get_commands (PALANG_APPLICATION (app));
  for (guint i = 0; i < g_list_model_get_n_items (commands); i++)
    {
      g_autoptr (PalangCommand) command = g_list_model_get_item (commands, i);
      g_autoptr (PalangPaletteItem) item = NULL;
      g_autofree char *shortcut = NULL;
      const char *accel = palang_command_get_accel (command);

      /* Listing the palette in itself would be odd. */
      if (g_str_equal (palang_command_get_action_name (command), "win.command-palette"))
        continue;

      /* "<Control><Alt>s" → "Ctrl+Alt+S", as the user sees it. */
      if (accel != NULL)
        {
          guint key;
          GdkModifierType modifiers;

          if (gtk_accelerator_parse (accel, &key, &modifiers))
            shortcut = gtk_accelerator_get_label (key, modifiers);
        }

      item = palang_palette_item_new (palang_command_get_action_name (command),
                                      palang_command_get_title (command), shortcut, NULL);
      g_list_store_append (items, item);
    }

  palette = palang_palette_new ("Run a command");
  palang_palette_set_items (palette, G_LIST_MODEL (items));
  g_signal_connect (palette, "item-activated", G_CALLBACK (on_command_chosen), self);
  show_palette (self, palette, "win.command-palette");
}

static void
on_file_chosen (PalangPalette     *palette,
                PalangPaletteItem *item,
                PalangWindow      *self)
{
  g_autoptr (GFile) file = NULL;

  if (palang_window_get_folder (self) == NULL)
    return;

  /* The id is the path relative to the folder. */
  file = g_file_resolve_relative_path (palang_window_get_folder (self),
                                       palang_palette_item_get_id (item));
  palang_window_open_file (self, file);
}

/* The project's files have been listed: show them in the palette, by name,
 * with the folder they're in as the subtitle. Typing matches the whole
 * relative path, so "app/win" finds src/app/palang-window.c. */
static void
on_quick_open_listed (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr (PalangPalette) palette = user_data;  /* reference taken in quick_open_action */
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) paths = palang_search_list_files_finish (result, &error);
  g_autoptr (GListStore) items = NULL;

  if (paths == NULL)
    {
      palang_palette_set_status (palette, error->message);
      return;
    }

  items = g_list_store_new (PALANG_TYPE_PALETTE_ITEM);
  for (guint i = 0; paths[i] != NULL; i++)
    {
      g_autofree char *shown = g_filename_display_name (paths[i]);
      g_autofree char *name = g_path_get_basename (shown);
      g_autofree char *folder = g_path_get_dirname (shown);
      g_autoptr (PalangPaletteItem) item = NULL;

      item = palang_palette_item_new (paths[i], name,
                                      g_str_equal (folder, ".") ? NULL : folder,
                                      shown);
      g_list_store_append (items, item);
    }

  palang_palette_set_status (palette, paths[0] == NULL ? "No files in this folder." : NULL);
  palang_palette_set_items (palette, G_LIST_MODEL (items));
}

/* Ctrl+P: open a file of the project folder by typing part of its name. */
static void
quick_open_action (GtkWidget  *widget,
                   const char *action_name,
                   GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  GFile *folder = palang_window_get_folder (self);
  PalangPalette *palette;

  if (palette_is_open (self, "win.quick-open"))
    return;

  if (folder == NULL)
    {
      _palang_window_show_toast (self, "Open a folder first (Ctrl+Shift+O) to find files in it.");
      return;
    }

  palette = palang_palette_new ("Open a file in the folder");
  palang_palette_set_status (palette, "Listing files…");
  g_signal_connect (palette, "item-activated", G_CALLBACK (on_file_chosen), self);
  show_palette (self, palette, "win.quick-open");

  /* Listing runs in the background; the palette is already open. */
  palang_search_list_files_async (folder, NULL, on_quick_open_listed, g_object_ref (palette));
}
/* ---- Setup, called from palang-window.c --------------------------------- */

void
_palang_window_class_init_palette (GtkWidgetClass *widget_class)
{
  gtk_widget_class_install_action (widget_class, "win.command-palette", NULL, command_palette_action);
  gtk_widget_class_install_action (widget_class, "win.quick-open", NULL, quick_open_action);
}
