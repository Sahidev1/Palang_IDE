/* palang-application.c — app startup, app-wide actions, window creation.
 *
 * GApplication lifecycle, in order:
 *   startup  — runs once when the app starts: initialize libraries, actions.
 *   activate — runs when the app is launched (also when the user launches it
 *              again while it is already running): show a window.
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
 * keyboard shortcuts can trigger without knowing how they are implemented. */
static void
palang_application_quit_action (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  g_application_quit (G_APPLICATION (user_data));
}

static const GActionEntry app_actions[] = {
  { .name = "quit", .activate = palang_application_quit_action },
};

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
  gtk_application_set_accels_for_action (GTK_APPLICATION (app), "app.quit",
                                         (const char *[]) { "<Control>q", NULL });
}

static void
palang_application_activate (GApplication *app)
{
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (app));

  /* Launching Palang again while it runs reuses the existing window. */
  if (window == NULL)
    window = GTK_WINDOW (palang_window_new (PALANG_APPLICATION (app)));

  gtk_window_present (window);
}

static void
palang_application_class_init (PalangApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  /* Override the lifecycle "virtual methods" with our own functions. */
  app_class->startup = palang_application_startup;
  app_class->activate = palang_application_activate;
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
                       "flags", G_APPLICATION_DEFAULT_FLAGS,
                       "resource-base-path", RESOURCE_PATH,
                       NULL);
}
