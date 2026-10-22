/* palang-terminal.c — the user's shell in a VTE terminal (see the header).
 *
 * VTE does the hard parts: the pseudo-terminal the shell talks to, escape
 * sequences, colors, selection, scrollback. This file decides which shell
 * to start and where, restarts it on request, and keeps the colors and
 * font in line with the rest of Palang.
 *
 * Starting a program is asynchronous (vte_terminal_spawn_async), like
 * everything that could take a moment: the UI keeps running and a callback
 * says how it went.
 */

#include "config.h"

#include "palang-settings.h"
#include "palang-terminal.h"

/* How many lines of output to keep for scrolling back (VTE's default is
 * 512, too few for a build log). */
#define SCROLLBACK_LINES 10000

struct _PalangTerminal
{
  AdwBin parent_instance;

  VteTerminal  *vte;          /* our child widget */
  GFile        *directory;    /* owned, nullable: where the shell starts */
  char        **environment;  /* owned, nullable: "NAME=value" extras */

  GCancellable *cancellable;  /* owned while a shell is starting */
  gboolean      starting;
  GPid          pid;          /* the running shell, or 0 */
};

G_DEFINE_FINAL_TYPE (PalangTerminal, palang_terminal, ADW_TYPE_BIN)

enum {
  PROP_0,
  PROP_DIRECTORY,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ---- Looks: colors and font --------------------------------------------- */

/* Matches the editor's background (palang-dark's "#0f0f0f" in dark mode,
 * data/styles/palang-dark.xml), so the terminal looks like part of the
 * window. The 16 palette colors (ls, compilers, …) stay VTE's defaults. */
static void
update_colors (PalangTerminal *self)
{
  gboolean dark = adw_style_manager_get_dark (adw_style_manager_get_default ());
  GdkRGBA foreground;
  GdkRGBA background;

  gdk_rgba_parse (&foreground, dark ? "#e8e8e8" : "#000000");
  gdk_rgba_parse (&background, dark ? "#0f0f0f" : "#ffffff");
  vte_terminal_set_colors (self->vte, &foreground, &background, NULL, 0);
}

static void
on_dark_changed (AdwStyleManager *style_manager,
                 GParamSpec      *pspec,
                 PalangTerminal  *self)
{
  update_colors (self);
}

/* The editor font setting, e.g. "Monospace 11". Empty means the system's
 * monospace font, which is what VTE uses when given NULL. */
static void
update_font (PalangTerminal *self)
{
  const char *font = palang_settings_get_font (palang_settings_get_default ());
  g_autoptr (PangoFontDescription) description = NULL;

  if (font != NULL && font[0] != '\0')
    description = pango_font_description_from_string (font);

  vte_terminal_set_font (self->vte, description);
}

static void
on_font_changed (PalangSettings *settings,
                 GParamSpec     *pspec,
                 PalangTerminal *self)
{
  update_font (self);
}

/* ---- Starting the shell ------------------------------------------------- */

/* Returns: (transfer full): the user's shell: $SHELL, else the one in their
 * account (/etc/passwd), else /bin/sh. */
static char *
find_shell (void)
{
  const char *from_environment = g_getenv ("SHELL");
  char *from_account;

  if (from_environment != NULL && from_environment[0] != '\0')
    return g_strdup (from_environment);

  from_account = vte_get_user_shell ();
  if (from_account != NULL)
    return from_account;

  return g_strdup ("/bin/sh");
}

/* Shows a note in the terminal itself, in VTE's "dim" style. Text fed to
 * VTE needs "\r\n" for a new line: "\n" alone only moves down. */
static void
show_note (PalangTerminal *self,
           const char     *note)
{
  g_autofree char *text = g_strdup_printf ("\r\n\033[2m%s\033[0m\r\n", note);

  vte_terminal_feed (self->vte, text, -1);
}

static void
on_spawned (VteTerminal *vte,
            GPid         pid,
            GError      *error,
            gpointer     user_data)
{
  g_autoptr (PalangTerminal) self = user_data;  /* reference taken in start */
  g_autofree char *note = NULL;

  self->starting = FALSE;
  g_clear_object (&self->cancellable);

  /* Cancelled: the terminal is being destroyed, nothing left to update. */
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  if (error != NULL)
    {
      note = g_strdup_printf ("Could not start the shell: %s — press Enter to try again",
                              error->message);
      show_note (self, note);
      return;
    }

  self->pid = pid;
}

void
palang_terminal_start (PalangTerminal *self)
{
  g_autofree char *shell = NULL;
  g_autofree char *directory = NULL;
  char *argv[2];

  g_return_if_fail (PALANG_IS_TERMINAL (self));

  if (self->pid != 0 || self->starting)
    return;

  /* An interactive shell, like a terminal window would start. */
  shell = find_shell ();
  argv[0] = shell;
  argv[1] = NULL;

  /* g_file_get_path () is NULL for non-local folders (sftp://…). */
  if (self->directory != NULL)
    directory = g_file_get_path (self->directory);
  if (directory == NULL)
    directory = g_strdup (g_get_home_dir ());

  self->starting = TRUE;
  self->cancellable = g_cancellable_new ();

  /* VTE adds self->environment to the environment Palang itself has. The
   * reference to self is released in on_spawned. */
  vte_terminal_spawn_async (self->vte, VTE_PTY_DEFAULT, directory, argv, self->environment,
                            G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
                            -1,  /* no timeout */
                            self->cancellable, on_spawned, g_object_ref (self));
}

/* VTE emits "child-exited" when the shell ends (exit, Ctrl+D, killed). */
static void
on_child_exited (VteTerminal    *vte,
                 int             status,
                 PalangTerminal *self)
{
  self->pid = 0;
  show_note (self, "Process exited — press Enter to restart");
}

/* The terminal was put on screen (the panel was opened): start the shell
 * now rather than when Palang starts, so a hidden terminal costs nothing. */
static void
on_map (GtkWidget      *widget,
        PalangTerminal *self)
{
  palang_terminal_start (self);
}

/* ---- Keys --------------------------------------------------------------- */

/* Runs before VTE sees a key (see the controller in init). Ctrl+C and
 * Ctrl+V belong to the shell (Ctrl+C interrupts a program), so copy and
 * paste use Ctrl+Shift+C and Ctrl+Shift+V, as in other terminals. */
static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                PalangTerminal        *self)
{
  /* Ignore modifiers like Num Lock that don't change shortcuts. */
  GdkModifierType modifiers = state & gtk_accelerator_get_default_mod_mask ();

  if (modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
    {
      /* With Shift held, the key value is the capital letter. */
      guint key = gdk_keyval_to_lower (keyval);

      if (key == GDK_KEY_c)
        {
          vte_terminal_copy_clipboard_format (self->vte, VTE_FORMAT_TEXT);
          return GDK_EVENT_STOP;
        }
      if (key == GDK_KEY_v)
        {
          vte_terminal_paste_clipboard (self->vte);
          return GDK_EVENT_STOP;
        }
    }

  /* After the shell exited (or failed to start): Enter starts a new one. */
  if (self->pid == 0 && !self->starting && modifiers == 0 &&
      (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter))
    {
      vte_terminal_reset (self->vte, TRUE, TRUE);  /* clear screen and history */
      palang_terminal_start (self);
      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

/* ---- Public API --------------------------------------------------------- */

void
palang_terminal_set_directory (PalangTerminal *self,
                               GFile          *directory)
{
  g_return_if_fail (PALANG_IS_TERMINAL (self));
  g_return_if_fail (directory == NULL || G_IS_FILE (directory));

  /* g_set_object: swaps the references and returns FALSE if nothing
   * changed, so we only notify on real changes. */
  if (g_set_object (&self->directory, directory))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTORY]);
}

GFile *
palang_terminal_get_directory (PalangTerminal *self)
{
  g_return_val_if_fail (PALANG_IS_TERMINAL (self), NULL);

  return self->directory;
}

void
palang_terminal_set_environment (PalangTerminal     *self,
                                 const char * const *environment)
{
  g_return_if_fail (PALANG_IS_TERMINAL (self));

  g_strfreev (self->environment);
  self->environment = g_strdupv ((char **) environment);
}

gboolean
palang_terminal_is_running (PalangTerminal *self)
{
  g_return_val_if_fail (PALANG_IS_TERMINAL (self), FALSE);

  return self->pid != 0;
}

gboolean
palang_terminal_forward_key (PalangTerminal        *self,
                             GtkEventControllerKey *controller)
{
  g_return_val_if_fail (PALANG_IS_TERMINAL (self), FALSE);
  g_return_val_if_fail (GTK_IS_EVENT_CONTROLLER_KEY (controller), FALSE);

  /* First our own keys (copy, paste, Enter to restart: on_key_pressed),
   * then VTE, which turns the key into input for the shell. */
  return gtk_event_controller_key_forward (controller, GTK_WIDGET (self)) ||
         gtk_event_controller_key_forward (controller, GTK_WIDGET (self->vte));
}

VteTerminal *
palang_terminal_get_vte (PalangTerminal *self)
{
  g_return_val_if_fail (PALANG_IS_TERMINAL (self), NULL);

  return self->vte;
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_terminal_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PalangTerminal *self = PALANG_TERMINAL (object);

  switch (prop_id)
    {
    case PROP_DIRECTORY:
      g_value_set_object (value, self->directory);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_terminal_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PalangTerminal *self = PALANG_TERMINAL (object);

  switch (prop_id)
    {
    case PROP_DIRECTORY:
      palang_terminal_set_directory (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_terminal_dispose (GObject *object)
{
  PalangTerminal *self = PALANG_TERMINAL (object);

  /* A shell still starting is abandoned. A running one gets a hangup
   * signal (SIGHUP) when VTE closes its terminal, like closing a terminal
   * window, and exits. VTE reports that exit ("child-exited") while it is
   * being destroyed, after we are gone, so stop listening first. */
  if (self->vte != NULL)
    g_signal_handlers_disconnect_by_data (self->vte, self);
  self->vte = NULL;  /* the AdwBin parent releases the widget itself */

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->directory);
  g_clear_pointer (&self->environment, g_strfreev);

  G_OBJECT_CLASS (palang_terminal_parent_class)->dispose (object);
}

static void
palang_terminal_class_init (PalangTerminalClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = palang_terminal_get_property;
  object_class->set_property = palang_terminal_set_property;
  object_class->dispose = palang_terminal_dispose;

  properties[PROP_DIRECTORY] =
    g_param_spec_object ("directory", NULL, NULL,
                         G_TYPE_FILE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
palang_terminal_init (PalangTerminal *self)
{
  GtkEventController *keys;

  self->vte = VTE_TERMINAL (vte_terminal_new ());
  vte_terminal_set_scrollback_lines (self->vte, SCROLLBACK_LINES);
  gtk_widget_set_vexpand (GTK_WIDGET (self->vte), TRUE);
  adw_bin_set_child (ADW_BIN (self), GTK_WIDGET (self->vte));

  g_signal_connect (self->vte, "child-exited", G_CALLBACK (on_child_exited), self);
  g_signal_connect (self, "map", G_CALLBACK (on_map), self);

  /* A key controller in the CAPTURE phase sees keys on their way down to
   * the focused widget, so it runs before VTE turns them into input for
   * the shell. */
  keys = gtk_event_controller_key_new ();
  gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
  g_signal_connect (keys, "key-pressed", G_CALLBACK (on_key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);

  /* The style manager and the settings live longer than any terminal.
   * g_signal_connect_object () disconnects by itself when self goes away,
   * so they never call us after we're freed. */
  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (on_dark_changed), self, 0);
  g_signal_connect_object (palang_settings_get_default (), "notify::font",
                           G_CALLBACK (on_font_changed), self, 0);
  update_colors (self);
  update_font (self);
}

PalangTerminal *
palang_terminal_new (void)
{
  return g_object_new (PALANG_TYPE_TERMINAL, NULL);
}
