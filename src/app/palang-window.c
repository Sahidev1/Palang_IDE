/* palang-window.c — the main window.
 *
 * The window's layout is described in data/ui/palang-window.ui (a GtkBuilder
 * "template"). This file adds the behaviour: for now, one GtkSourceView with
 * C syntax highlighting whose colour theme follows the system light/dark
 * setting.
 *
 * Phase 1 replaces the single editor with a file tree and tabs.
 */

#include "config.h"

#include <gtksourceview/gtksource.h>

#include "palang-window.h"

struct _PalangWindow
{
  AdwApplicationWindow parent_instance;

  /* Template child: GTK fills this pointer in from the .ui file (see
   * gtk_widget_class_bind_template_child below). The window owns it. */
  GtkSourceView *source_view;
};

G_DEFINE_FINAL_TYPE (PalangWindow, palang_window, ADW_TYPE_APPLICATION_WINDOW)

static GtkSourceBuffer *
get_source_buffer (PalangWindow *self)
{
  /* A GtkSourceView always creates a GtkSourceBuffer as its buffer, so this
   * cast is safe. "transfer none": we don't own the returned buffer. */
  return GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->source_view)));
}

static void
update_style_scheme (PalangWindow *self)
{
  AdwStyleManager *style_manager = adw_style_manager_get_default ();
  GtkSourceStyleSchemeManager *schemes = gtk_source_style_scheme_manager_get_default ();
  const char *scheme_id;

  /* "Adwaita" and "Adwaita-dark" ship with GtkSourceView 5 and match the
   * libadwaita look. */
  scheme_id = adw_style_manager_get_dark (style_manager) ? "Adwaita-dark" : "Adwaita";

  gtk_source_buffer_set_style_scheme (get_source_buffer (self),
                                      gtk_source_style_scheme_manager_get_scheme (schemes, scheme_id));
}

/* Called whenever AdwStyleManager's "dark" property changes, i.e. when the
 * user switches the system between light and dark mode. */
static void
on_dark_changed (AdwStyleManager *style_manager,
                 GParamSpec      *pspec,
                 PalangWindow    *self)
{
  update_style_scheme (self);
}

static void
palang_window_dispose (GObject *object)
{
  /* Release the template children before the window itself goes away. */
  gtk_widget_dispose_template (GTK_WIDGET (object), PALANG_TYPE_WINDOW);

  G_OBJECT_CLASS (palang_window_parent_class)->dispose (object);
}

static void
palang_window_class_init (PalangWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = palang_window_dispose;

  /* The .ui file uses <object class="GtkSourceView">, so that type must be
   * registered before the template is parsed. */
  g_type_ensure (GTK_SOURCE_TYPE_VIEW);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-window.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangWindow, source_view);
}

static void
palang_window_init (PalangWindow *self)
{
  GtkSourceLanguageManager *languages = gtk_source_language_manager_get_default ();
  GtkSourceBuffer *buffer;

  /* Build the widgets described in the template. After this call,
   * self->source_view is valid. */
  gtk_widget_init_template (GTK_WIDGET (self));

  buffer = get_source_buffer (self);
  gtk_source_buffer_set_language (buffer,
                                  gtk_source_language_manager_get_language (languages, "c"));
  gtk_text_buffer_set_text (GTK_TEXT_BUFFER (buffer),
                            "/* Welcome to Palang.\n"
                            " * Opening files arrives in Phase 1. */\n"
                            "\n"
                            "int\n"
                            "main (void)\n"
                            "{\n"
                            "  return 0;\n"
                            "}\n",
                            -1);

  update_style_scheme (self);

  /* g_signal_connect_object (not plain g_signal_connect) disconnects the
   * handler automatically when the window is destroyed, so the style manager
   * (which lives for the whole app) never calls into a freed window. */
  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (on_dark_changed), self, 0);
}

PalangWindow *
palang_window_new (PalangApplication *application)
{
  return g_object_new (PALANG_TYPE_WINDOW,
                       "application", application,
                       NULL);
}
