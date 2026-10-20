/* palang-application.c — app startup, app-wide actions, window creation.
 *
 * GApplication lifecycle, in order:
 *   startup  — runs once when the app starts: initialize libraries, actions.
 *   activate — runs when the app is launched (also when the user launches it
 *              again while it is already running): show a window.
 *   open     — like activate, but with files to open ("palang main.c").
 *   shutdown — runs once before exit.
 */

#include "config.h"

#include <gtksourceview/gtksource.h>

#include "palang-application.h"
#include "palang-window.h"

struct _PalangApplication
{
  AdwApplication parent_instance;
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

static const GActionEntry app_actions[] = {
  { .name = "quit", .activate = palang_application_quit_action },
};

/* Keyboard shortcuts: action name → accelerators. "win." actions are
 * defined in palang-window.c. */
static const struct {
  const char *action;
  const char *accels[2];
} shortcuts[] = {
  { "app.quit",      { "<Control>q", NULL } },
  { "win.new",       { "<Control>n", NULL } },
  { "win.open",      { "<Control>o", NULL } },
  { "win.save",      { "<Control>s", NULL } },
  { "win.save-as",   { "<Control><Shift>s", NULL } },
  { "win.save-all",  { "<Control><Alt>s", NULL } },
  { "win.close-tab", { "<Control>w", NULL } },
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

  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   app_actions, G_N_ELEMENTS (app_actions),
                                   app);
  for (guint i = 0; i < G_N_ELEMENTS (shortcuts); i++)
    gtk_application_set_accels_for_action (GTK_APPLICATION (app),
                                           shortcuts[i].action, shortcuts[i].accels);
}

static void
palang_application_activate (GApplication *app)
{
  /* Launching Palang again while it runs reuses the existing window. */
  gtk_window_present (GTK_WINDOW (get_or_create_window (app)));
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
    palang_window_open_file (window, files[i]);

  gtk_window_present (GTK_WINDOW (window));
}

static void
palang_application_class_init (PalangApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  /* Override the lifecycle "virtual methods" with our own functions. */
  app_class->startup = palang_application_startup;
  app_class->activate = palang_application_activate;
  app_class->open = palang_application_open;
}

static void
palang_application_init (PalangApplication *self)
{
}

PalangApplication *
palang_application_new (void)
{
  /* The application ID must be unique; GLib uses it to make sure only one
   * Palang process runs per user session (a second launch just activates
   * the first one). */
  return g_object_new (PALANG_TYPE_APPLICATION,
                       "application-id", APPLICATION_ID,
                       /* HANDLES_OPEN: accept files on the command line. */
                       "flags", G_APPLICATION_HANDLES_OPEN,
                       "resource-base-path", RESOURCE_PATH,
                       NULL);
}
