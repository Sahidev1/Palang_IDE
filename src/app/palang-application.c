/* palang-application.c — app startup, app-wide actions, window creation.
 *
 * GApplication lifecycle, in order:
 *   startup  — runs once when the app starts: initialize libraries, actions.
 *   activate — runs when the app is launched (also when the user launches it
 *              again while it is already running): show a window, restoring
 *              what was open last time.
 *   open     — like activate, but with files or a folder to open
 *              ("palang main.c", "palang ~/projects/foo").
 *   shutdown — runs once before exit.
 *
 * Every launch goes through command_line first, which picks activate or
 * open, or runs a `palang` subcommand ("palang set tab-width 2", typed in
 * the Palang terminal). A second `palang` process doesn't run any of this
 * itself: GApplication sends its arguments to the Palang already running
 * (over D-Bus), which handles them and sends back the output and exit
 * status.
 */

#include "config.h"

#include <gtksourceview/gtksource.h>

#include "palang-appearance.h"
#include "palang-application.h"
#include "palang-command.h"
#include "palang-command-line.h"
#include "palang-preferences-dialog.h"
#include "palang-settings.h"
#include "palang-window.h"

struct _PalangApplication
{
  AdwApplication parent_instance;

  GListStore *commands;  /* PalangCommand, built from commands[] */
};

/* Defines palang_application_get_type() and the parent class pointer
 * `palang_application_parent_class`, used below for chaining up. */
G_DEFINE_FINAL_TYPE (PalangApplication, palang_application, ADW_TYPE_APPLICATION)

/* "app.quit" action. Actions are named commands that menus, buttons and
 * keyboard shortcuts can trigger without knowing how they are implemented.
 *
 * Quitting closes each window the normal way, so windows with unsaved
 * changes can ask first. When the last window is gone, GtkApplication
 * exits by itself. */
static void
palang_application_quit_action (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  GtkApplication *app = GTK_APPLICATION (user_data);
  /* Copy the list: closing a window removes it from the original. */
  g_autoptr (GList) windows = g_list_copy (gtk_application_get_windows (app));

  for (GList *l = windows; l != NULL; l = l->next)
    gtk_window_close (GTK_WINDOW (l->data));
}

/* "app.preferences": show the Preferences window over the current window,
 * unless it is already showing (pressing Ctrl+, twice). */
static void
palang_application_preferences_action (GSimpleAction *action,
                                       GVariant      *parameter,
                                       gpointer       user_data)
{
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (user_data));

  if (ADW_IS_APPLICATION_WINDOW (window) &&
      PALANG_IS_PREFERENCES_DIALOG (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window))))
    return;

  adw_dialog_present (palang_preferences_dialog_new (palang_settings_get_default ()),
                      GTK_WIDGET (window));
}

static const GActionEntry app_actions[] = {
  { .name = "quit", .activate = palang_application_quit_action },
  { .name = "preferences", .activate = palang_application_preferences_action },
};

/* The command table: every command of the app, its name for people and its
 * keyboard shortcut. It feeds both the shortcuts (in startup) and the
 * command palette (palang_application_get_commands ()), so a new command
 * added here shows up in both. "win." actions are defined in
 * palang-window.c. accel is NULL for a command without a shortcut. */
static const struct {
  const char *action;
  const char *title;
  const char *accel;
} commands[] = {
  { "win.command-palette", "Command Palette",      "<Control><Shift>p" },
  { "win.quick-open",      "Open File in Folder…", "<Control>p" },
  { "win.new",             "New File",             "<Control>n" },
  { "win.open",            "Open File…",           "<Control>o" },
  { "win.open-folder",     "Open Folder…",         "<Control><Shift>o" },
  { "win.save",            "Save",                 "<Control>s" },
  { "win.save-as",         "Save As…",             "<Control><Shift>s" },
  { "win.save-all",        "Save All",             "<Control><Alt>s" },
  { "win.close-tab",       "Close Tab",            "<Control>w" },
  { "win.find",            "Find…",                "<Control>f" },
  { "win.replace",         "Find and Replace…",    "<Control>h" },
  { "win.find-next",       "Find Next",            "F3" },
  { "win.find-previous",   "Find Previous",        "<Shift>F3" },
  { "win.go-to-line",      "Go to Line…",          "<Control>g" },
  { "win.go-to-definition", "Go to Definition",    "F12" },
  { "win.show-completions", "Show Completions",    "<Control>space" },
  { "win.toggle-sidebar",  "Toggle Sidebar",       "F9" },
  { "win.toggle-panel",    "Toggle Panel",         "<Control>j" },
  { "win.search-project",  "Search in Folder…",    "<Control><Shift>f" },
  { "win.show-problems",   "Show Problems",        "<Control><Shift>m" },
  { "win.restart-language-servers", "Restart Language Servers", NULL },
  { "app.preferences",     "Preferences",          "<Control>comma" },
  { "app.quit",            "Quit",                 "<Control>q" },
};

/* Returns: (transfer none): the existing window, or a new one. */
static PalangWindow *
get_or_create_window (GApplication *app)
{
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (app));

  if (window == NULL)
    window = GTK_WINDOW (palang_window_new (PALANG_APPLICATION (app)));

  return PALANG_WINDOW (window);
}

static void
palang_application_startup (GApplication *app)
{
  /* "Chain up": let the parent class (AdwApplication) do its own startup
   * first. It initializes GTK and libadwaita. */
  G_APPLICATION_CLASS (palang_application_parent_class)->startup (app);

  /* Registers GtkSourceView's types and loads its languages and themes. */
  gtk_source_init ();

  /* Light/dark and the editor font follow the user's settings. */
  palang_appearance_follow (palang_settings_get_default ());

  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   app_actions, G_N_ELEMENTS (app_actions),
                                   app);
  for (guint i = 0; i < G_N_ELEMENTS (commands); i++)
    {
      /* An action can have several shortcuts, so GTK takes a
       * NULL-terminated list; ours have one at most. */
      const char *accels[] = { commands[i].accel, NULL };

      if (commands[i].accel != NULL)
        gtk_application_set_accels_for_action (GTK_APPLICATION (app),
                                               commands[i].action, accels);
    }
}

static void
palang_application_activate (GApplication *app)
{
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (app));

  /* Launching Palang again while it runs reuses the existing window. A new
   * window starts with what was open last time. */
  if (window == NULL)
    {
      g_autofree char *path = palang_session_default_path ();
      g_autoptr (PalangSession) session = palang_session_load (path);

      window = GTK_WINDOW (palang_window_new (PALANG_APPLICATION (app)));
      if (session != NULL)
        palang_window_restore_session (PALANG_WINDOW (window), session);
    }

  gtk_window_present (window);
}

/* "palang file1.c file2.c" — called instead of activate when files are
 * given on the command line (or opened from the file manager). */
static void
palang_application_open (GApplication  *app,
                         GFile        **files,
                         int            n_files,
                         const char    *hint)
{
  PalangWindow *window = get_or_create_window (app);

  for (int i = 0; i < n_files; i++)
    {
      /* A folder becomes the sidebar's folder; anything else opens in a
       * tab. (A quick, local check, so doing it synchronously is fine.) */
      if (g_file_query_file_type (files[i], G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY)
        palang_window_open_folder (window, files[i]);
      else
        palang_window_open_file (window, files[i]);
    }

  gtk_window_present (GTK_WINDOW (window));
}

GListModel *
palang_application_get_commands (PalangApplication *self)
{
  g_return_val_if_fail (PALANG_IS_APPLICATION (self), NULL);

  return G_LIST_MODEL (self->commands);
}

static void
palang_application_dispose (GObject *object)
{
  PalangApplication *self = PALANG_APPLICATION (object);

  g_clear_object (&self->commands);

  G_OBJECT_CLASS (palang_application_parent_class)->dispose (object);
}

/* Runs in the running Palang for every launch: its own, and each `palang`
 * typed later anywhere (@command_line describes that call: arguments,
 * environment, folder, and where to print).
 *
 * Subcommands ("palang set …") count only when the caller runs in the
 * Palang terminal, recognized by PALANG_TERMINAL=1 in *its* environment.
 * Elsewhere `palang settings` opens a file called "settings", as before, so
 * the names can't clash with other uses. (This avoids clashes; it is not a
 * security boundary: anyone can set the variable.)
 *
 * Returns: the exit status of the caller's `palang` process. */
static int
palang_application_command_line (GApplication            *app,
                                 GApplicationCommandLine *command_line)
{
  int argc;
  g_auto (GStrv) argv = g_application_command_line_get_arguments (command_line, &argc);
  const char *in_terminal = g_application_command_line_getenv (command_line, "PALANG_TERMINAL");
  g_autoptr (GPtrArray) files = NULL;

  if (argc > 1 && g_strcmp0 (in_terminal, "1") == 0 &&
      palang_command_line_is_subcommand (argv[1]))
    {
      g_autoptr (GString) output = g_string_new (NULL);
      g_autoptr (GString) errors = g_string_new (NULL);
      int status = palang_command_line_run (palang_settings_get_default (),
                                            (const char * const *) argv + 1,
                                            output, errors);

      /* _literal: print the text as is (no printf-style % handling). */
      if (output->len > 0)
        g_application_command_line_print_literal (command_line, output->str);
      if (errors->len > 0)
        g_application_command_line_printerr_literal (command_line, errors->str);
      return status;
    }

  if (argc <= 1)
    {
      g_application_activate (app);
      return 0;
    }

  /* File names are relative to the caller's current folder, not ours;
   * create_file_for_arg () takes care of that. */
  files = g_ptr_array_new_with_free_func (g_object_unref);
  for (int i = 1; i < argc; i++)
    g_ptr_array_add (files, g_application_command_line_create_file_for_arg (command_line, argv[i]));

  g_application_open (app, (GFile **) files->pdata, files->len, "");
  return 0;
}

static void
palang_application_class_init (PalangApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  object_class->dispose = palang_application_dispose;

  /* Override the lifecycle "virtual methods" with our own functions. */
  app_class->startup = palang_application_startup;
  app_class->activate = palang_application_activate;
  app_class->open = palang_application_open;
  app_class->command_line = palang_application_command_line;
}

static void
palang_application_init (PalangApplication *self)
{
  /* GListStore is GLib's ready-made GListModel: a list of objects of one
   * type that tells its users (like a list widget) when it changes. */
  self->commands = g_list_store_new (PALANG_TYPE_COMMAND);

  for (guint i = 0; i < G_N_ELEMENTS (commands); i++)
    {
      g_autoptr (PalangCommand) command = palang_command_new (commands[i].action,
                                                              commands[i].title,
                                                              commands[i].accel);
      g_list_store_append (self->commands, command);
    }
}

PalangApplication *
palang_application_new (void)
{
  /* The application ID must be unique; GLib uses it to make sure only one
   * Palang process runs per user session (a second launch just activates
   * the first one). */
  return g_object_new (PALANG_TYPE_APPLICATION,
                       "application-id", APPLICATION_ID,
                       /* HANDLES_COMMAND_LINE: the running Palang gets every
                        * launch's arguments (palang_application_command_line).
                        * SEND_ENVIRONMENT: and its environment variables
                        * (for PALANG_TERMINAL).
                        * HANDLES_OPEN: we can open files (g_application_open). */
                       "flags", G_APPLICATION_HANDLES_COMMAND_LINE |
                                G_APPLICATION_SEND_ENVIRONMENT |
                                G_APPLICATION_HANDLES_OPEN,
                       "resource-base-path", RESOURCE_PATH,
                       NULL);
}
