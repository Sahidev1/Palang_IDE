/* palang-window-panel.c — PalangWindow's bottom panel and its pages.
 *
 * Part of PalangWindow (see palang-window-private.h for how the window's
 * code is split). The panel (PalangPanel, palang-panel.c) sits under the
 * tabs in a vertical GtkPaned; Ctrl+J shows and hides it. This file:
 *
 *   - keeps the panel's height when it is hidden and shown again
 *   - creates the Terminal and Search pages and connects them
 *   - hands keys to the terminal while it has the focus
 *   - the actions win.toggle-panel (Ctrl+J) and win.search-project
 */

#include "config.h"

#include "palang-window-private.h"

/* Height of the bottom panel when nothing was saved. */
#define DEFAULT_PANEL_HEIGHT 250

/* The panel's height as GtkPaned sees it: from the divider to the bottom,
 * i.e. the paned's height minus the divider's position (which GtkPaned
 * counts from the top). Measured this way, putting the divider back at
 * "paned height − panel height" gives exactly the same layout again. */
int
_palang_window_get_panel_height (PalangWindow *self)
{
  int paned_height = gtk_widget_get_height (GTK_WIDGET (self->paned));

  if (!gtk_widget_get_visible (GTK_WIDGET (self->panel)) || paned_height <= 0)
    return self->panel_height;

  return paned_height - gtk_paned_get_position (self->paned);
}

/* Gives the panel self->panel_height by moving the divider. This needs the
 * paned's own height, which only exists once the window has been laid out
 * (not yet when a session is restored at startup).
 *
 * A "tick callback" runs before every frame GTK draws for a widget; it
 * keeps running until it returns G_SOURCE_REMOVE. We use it to wait for
 * the first frame in which the paned has a height. */
static gboolean
restore_panel_height (GtkWidget     *paned,
                      GdkFrameClock *frame_clock,
                      gpointer       user_data)
{
  PalangWindow *self = PALANG_WINDOW (user_data);
  int paned_height = gtk_widget_get_height (paned);

  if (paned_height <= 0)
    return G_SOURCE_CONTINUE;

  gtk_paned_set_position (self->paned, MAX (0, paned_height - self->panel_height));
  return G_SOURCE_REMOVE;
}

/* The panel was shown or hidden (Ctrl+J, its close button, the header bar
 * button, a restored session).
 *
 * Shown: it gets back the height it had (or the default). While it was
 * hidden the tabs had all the room, and window resizes in the meantime
 * moved the divider, so we put the divider back ourselves. Without that, a
 * panel shown at startup would also get just its minimum height (the
 * switcher row).
 *
 * Hidden: remember its height. And if it had the keyboard focus, GTK would
 * leave the focus nowhere; give it back to the text instead. */
static void
on_panel_visible_changed (PalangPanel  *panel,
                          GParamSpec   *pspec,
                          PalangWindow *self)
{
  GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (self));
  PalangEditor *editor = palang_window_get_active_editor (self);

  if (gtk_widget_get_visible (GTK_WIDGET (panel)))
    {
      gtk_widget_add_tick_callback (GTK_WIDGET (self->paned), restore_panel_height, self, NULL);
      return;
    }

  /* The divider hasn't moved yet, so this is still the height it had. */
  if (gtk_widget_get_height (GTK_WIDGET (self->paned)) > 0)
    self->panel_height = gtk_widget_get_height (GTK_WIDGET (self->paned)) -
                         gtk_paned_get_position (self->paned);

  if (editor != NULL &&
      (focus == NULL || gtk_widget_is_ancestor (focus, GTK_WIDGET (panel))))
    gtk_widget_grab_focus (GTK_WIDGET (editor));
}

/* The terminal page. Its shell starts in the project folder: the terminal
 * follows the file tree's root (a property binding), and uses it the next
 * time a shell starts.
 *
 * The shell also gets PALANG_TERMINAL=1, and the folder of the running
 * Palang program first in PATH, so that typing `palang` in it runs this very
 * Palang (in development: build/src/palang, not an installed copy). */
static void
add_terminal_page (PalangWindow *self)
{
  g_autofree char *program = g_file_read_link ("/proc/self/exe", NULL);
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_auto (GStrv) environment = NULL;

  self->terminal = palang_terminal_new ();

  g_strv_builder_add (builder, "PALANG_TERMINAL=1");
  if (program != NULL)
    {
      g_autofree char *program_folder = g_path_get_dirname (program);
      const char *old_path = g_getenv ("PATH");

      if (old_path != NULL && old_path[0] != '\0')
        g_strv_builder_take (builder, g_strdup_printf ("PATH=%s:%s", program_folder, old_path));
      else
        g_strv_builder_take (builder, g_strdup_printf ("PATH=%s", program_folder));
    }
  environment = g_strv_builder_end (builder);
  palang_terminal_set_environment (self->terminal, (const char * const *) environment);

  g_object_bind_property (self->file_tree, "root", self->terminal, "directory",
                          G_BINDING_SYNC_CREATE);

  palang_panel_add_page (self->panel, GTK_WIDGET (self->terminal),
                         "terminal", "Terminal", "utilities-terminal-symbolic");
}

/* While the terminal has the keyboard focus, keys belong to the shell:
 * Ctrl+W deletes a word, Ctrl+R searches the history, and so on. Only the
 * commands listed here keep their shortcuts there; their keys come from
 * the command table (palang-application.c). */
static const char * const commands_kept_in_terminal[] = {
  "win.toggle-panel",
  "win.toggle-sidebar",
  "win.search-project",
  "win.show-problems",
  "win.command-palette",
  "win.quick-open",
  "app.preferences",
  "app.quit",
};

/* TRUE if the key being pressed is the shortcut of one of
 * commands_kept_in_terminal. */
static gboolean
is_kept_shortcut (PalangWindow       *self,
                  GtkEventController *controller)
{
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  GdkEvent *event = gtk_event_controller_get_current_event (controller);

  if (app == NULL)
    return FALSE;

  for (guint i = 0; i < G_N_ELEMENTS (commands_kept_in_terminal); i++)
    {
      g_auto (GStrv) accels = gtk_application_get_accels_for_action (app, commands_kept_in_terminal[i]);

      for (guint j = 0; accels[j] != NULL; j++)
        {
          /* A GtkShortcutTrigger knows how to compare a key press with a
           * shortcut like "<Control>j" (Shift, Caps Lock, keyboard layouts). */
          g_autoptr (GtkShortcutTrigger) trigger = gtk_shortcut_trigger_parse_string (accels[j]);

          if (trigger != NULL &&
              gtk_shortcut_trigger_trigger (trigger, event, FALSE) == GDK_KEY_MATCH_EXACT)
            return TRUE;
        }
    }

  return FALSE;
}

/* Runs for every key pressed in the window, before the application's
 * shortcuts: both act in the "capture phase" (on the way from the window
 * down to the focused widget), and this controller, added in init, comes
 * first (checked: on Ctrl+W it runs while the tab is still open). */
static gboolean
on_window_key_pressed (GtkEventControllerKey *controller,
                       guint                  keyval,
                       guint                  keycode,
                       GdkModifierType        state,
                       PalangWindow          *self)
{
  GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (self));

  if (focus == NULL || !gtk_widget_is_ancestor (focus, GTK_WIDGET (self->terminal)))
    return GDK_EVENT_PROPAGATE;

  if (is_kept_shortcut (self, GTK_EVENT_CONTROLLER (controller)))
    return GDK_EVENT_PROPAGATE;

  return palang_terminal_forward_key (self->terminal, controller) ? GDK_EVENT_STOP
                                                                  : GDK_EVENT_PROPAGATE;
}

/* The search page. It searches the project folder (bound to the file
 * tree's root, like the terminal), and a clicked result opens its file at
 * the match. */
static void
on_search_match_activated (PalangSearchPanel *panel,
                           PalangSearchMatch *match,
                           PalangWindow      *self)
{
  palang_window_open_file_at (self, palang_search_match_get_file (match),
                              palang_search_match_get_line (match),
                              palang_search_match_get_column (match));
}

static void
add_search_page (PalangWindow *self)
{
  self->search_panel = palang_search_panel_new ();

  g_object_bind_property (self->file_tree, "root", self->search_panel, "folder",
                          G_BINDING_SYNC_CREATE);
  g_signal_connect (self->search_panel, "match-activated",
                    G_CALLBACK (on_search_match_activated), self);

  palang_panel_add_page (self->panel, GTK_WIDGET (self->search_panel),
                         "search", "Search", "system-search-symbolic");
}

PalangSearchPanel *
palang_window_get_search_panel (PalangWindow *self)
{
  g_return_val_if_fail (PALANG_IS_WINDOW (self), NULL);

  return self->search_panel;
}

PalangTerminal *
palang_window_get_terminal (PalangWindow *self)
{
  g_return_val_if_fail (PALANG_IS_WINDOW (self), NULL);

  return self->terminal;
}

PalangPanel *
palang_window_get_panel (PalangWindow *self)
{
  g_return_val_if_fail (PALANG_IS_WINDOW (self), NULL);

  return self->panel;
}

/* Ctrl+J. Showing the panel also moves the keyboard focus into it, so
 * Ctrl+J is enough to get to the terminal and back. */
static void
toggle_panel_action (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  gboolean show = !gtk_widget_get_visible (GTK_WIDGET (self->panel));

  gtk_widget_set_visible (GTK_WIDGET (self->panel), show);
  if (show)
    palang_panel_focus_page (self->panel);
}

/* Ctrl+Shift+F: the Search page, with the selected text (if it's on one
 * line) as what to search for. */
static void
search_project_action (GtkWidget  *widget,
                       const char *action_name,
                       GVariant   *parameter)
{
  PalangWindow *self = PALANG_WINDOW (widget);
  PalangEditor *editor = palang_window_get_active_editor (self);
  g_autofree char *selected = NULL;
  GtkTextIter start, end;

  if (editor != NULL &&
      gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (palang_editor_get_document (editor)),
                                            &start, &end) &&
      gtk_text_iter_get_line (&start) == gtk_text_iter_get_line (&end))
    selected = gtk_text_iter_get_text (&start, &end);

  gtk_widget_set_visible (GTK_WIDGET (self->panel), TRUE);
  palang_panel_set_visible_page (self->panel, "search");
  palang_search_panel_focus (self->search_panel, selected);
}
/* ---- Setup, called from palang-window.c --------------------------------- */

void
_palang_window_class_init_panel (GtkWidgetClass *widget_class)
{
  gtk_widget_class_install_action (widget_class, "win.toggle-panel", NULL, toggle_panel_action);
  gtk_widget_class_install_action (widget_class, "win.search-project", NULL, search_project_action);
}

void
_palang_window_init_key_routing (PalangWindow *self)
{
  GtkEventController *keys = gtk_event_controller_key_new ();

  gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
  g_signal_connect (keys, "key-pressed", G_CALLBACK (on_window_key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);
}

void
_palang_window_init_panel (PalangWindow *self)
{
  self->panel_height = DEFAULT_PANEL_HEIGHT;

  g_signal_connect (self->panel, "notify::visible",
                    G_CALLBACK (on_panel_visible_changed), self);

  add_terminal_page (self);
  add_search_page (self);
}
