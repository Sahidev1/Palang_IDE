/* palang-editor.c — one document shown in a GtkSourceView.
 *
 * The view's settings (line numbers, indentation…) are in
 * data/ui/palang-editor.ui. This file connects the view to its document and
 * keeps the "title" and "path" properties up to date, which the window binds
 * to the tab's label and tooltip.
 */

#include "config.h"

#include "palang-editor.h"

struct _PalangEditor
{
  AdwBin parent_instance;

  PalangDocument *document;  /* owned */

  /* Template child */
  GtkSourceView *view;
};

G_DEFINE_FINAL_TYPE (PalangEditor, palang_editor, ADW_TYPE_BIN)

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_TITLE,
  PROP_PATH,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
update_style_scheme (PalangEditor *self)
{
  AdwStyleManager *style_manager = adw_style_manager_get_default ();
  GtkSourceStyleSchemeManager *schemes = gtk_source_style_scheme_manager_get_default ();
  const char *scheme_id;

  /* "Adwaita" and "Adwaita-dark" ship with GtkSourceView 5 and match the
   * libadwaita look. (Phase 1.5 replaces this with a user setting.) */
  scheme_id = adw_style_manager_get_dark (style_manager) ? "Adwaita-dark" : "Adwaita";

  gtk_source_buffer_set_style_scheme (GTK_SOURCE_BUFFER (self->document),
                                      gtk_source_style_scheme_manager_get_scheme (schemes, scheme_id));
}

static void
on_dark_changed (AdwStyleManager *style_manager,
                 GParamSpec      *pspec,
                 PalangEditor    *self)
{
  update_style_scheme (self);
}

/* The document's name changed (Save As) or its modified flag flipped: the
 * tab label and tooltip need updating. */
static void
on_document_title_changed (PalangDocument *document,
                           GParamSpec     *pspec,
                           PalangEditor   *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PATH]);
}

static void
on_document_modified_changed (GtkTextBuffer *buffer,
                              PalangEditor  *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

char *
palang_editor_dup_title (PalangEditor *self)
{
  g_autofree char *title = NULL;

  g_return_val_if_fail (PALANG_IS_EDITOR (self), NULL);

  title = palang_document_dup_title (self->document);

  if (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self->document)))
    return g_strdup_printf ("• %s", title);

  return g_steal_pointer (&title);
}

char *
palang_editor_dup_path (PalangEditor *self)
{
  GFile *file;
  g_autoptr (GFile) home = NULL;
  g_autofree char *relative = NULL;

  g_return_val_if_fail (PALANG_IS_EDITOR (self), NULL);

  file = palang_document_get_file (self->document);
  if (file == NULL)
    return NULL;

  /* Show "~/projects/main.c" instead of "/home/ali/projects/main.c". */
  home = g_file_new_for_path (g_get_home_dir ());
  relative = g_file_get_relative_path (home, file);
  if (relative != NULL)
    return g_build_filename ("~", relative, NULL);

  /* Not a local file, or outside the home folder. g_file_get_parse_name
   * gives a readable form either way (a path or a URI). */
  return g_file_get_parse_name (file);
}

PalangDocument *
palang_editor_get_document (PalangEditor *self)
{
  g_return_val_if_fail (PALANG_IS_EDITOR (self), NULL);

  return self->document;
}

GtkSourceView *
palang_editor_get_view (PalangEditor *self)
{
  g_return_val_if_fail (PALANG_IS_EDITOR (self), NULL);

  return self->view;
}

/* Clicking or tabbing into the editor focuses the text view. */
static gboolean
palang_editor_grab_focus (GtkWidget *widget)
{
  return gtk_widget_grab_focus (GTK_WIDGET (PALANG_EDITOR (widget)->view));
}

static void
palang_editor_constructed (GObject *object)
{
  PalangEditor *self = PALANG_EDITOR (object);

  G_OBJECT_CLASS (palang_editor_parent_class)->constructed (object);

  /* The "document" property has been set by now (it's construct-only). */
  gtk_text_view_set_buffer (GTK_TEXT_VIEW (self->view), GTK_TEXT_BUFFER (self->document));

  g_signal_connect_object (self->document, "notify::title",
                           G_CALLBACK (on_document_title_changed), self, 0);
  g_signal_connect_object (self->document, "modified-changed",
                           G_CALLBACK (on_document_modified_changed), self, 0);

  update_style_scheme (self);
  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (on_dark_changed), self, 0);
}

static void
palang_editor_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PalangEditor *self = PALANG_EDITOR (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, self->document);
      break;

    case PROP_TITLE:
      g_value_take_string (value, palang_editor_dup_title (self));
      break;

    case PROP_PATH:
      g_value_take_string (value, palang_editor_dup_path (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_editor_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  PalangEditor *self = PALANG_EDITOR (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      /* dup_object: we keep our own reference to the document. */
      self->document = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_editor_dispose (GObject *object)
{
  PalangEditor *self = PALANG_EDITOR (object);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_EDITOR);
  g_clear_object (&self->document);

  G_OBJECT_CLASS (palang_editor_parent_class)->dispose (object);
}

static void
palang_editor_class_init (PalangEditorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = palang_editor_constructed;
  object_class->get_property = palang_editor_get_property;
  object_class->set_property = palang_editor_set_property;
  object_class->dispose = palang_editor_dispose;

  widget_class->grab_focus = palang_editor_grab_focus;

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         PALANG_TYPE_DOCUMENT,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  properties[PROP_PATH] =
    g_param_spec_string ("path", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  g_type_ensure (GTK_SOURCE_TYPE_VIEW);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-editor.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangEditor, view);
}

static void
palang_editor_init (PalangEditor *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

PalangEditor *
palang_editor_new (PalangDocument *document)
{
  g_return_val_if_fail (PALANG_IS_DOCUMENT (document), NULL);

  return g_object_new (PALANG_TYPE_EDITOR,
                       "document", document,
                       NULL);
}
