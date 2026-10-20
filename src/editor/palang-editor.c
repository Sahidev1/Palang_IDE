/* palang-editor.c — one document shown in a GtkSourceView.
 *
 * The view's settings (line numbers, indentation…) are in
 * data/ui/palang-editor.ui. This file connects the view to its document and
 * keeps the "title" and "path" properties up to date, which the window binds
 * to the tab's label and tooltip.
 */

#include "config.h"

#include "palang-editor.h"
#include "palang-settings.h"

struct _PalangEditor
{
  AdwBin parent_instance;

  PalangDocument  *document;    /* owned */
  PalangSearchBar *search_bar;  /* child widget, created in init */

  /* Template children */
  GtkSourceView *view;
  GtkRevealer   *go_to_revealer;
  GtkEntry      *go_to_entry;
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
   * libadwaita look. Whether we're dark is decided by the "Style" setting
   * (see palang-appearance.c). */
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

/* ---- Find / Replace ----------------------------------------------------- */

PalangSearchBar *
palang_editor_get_search_bar (PalangEditor *self)
{
  g_return_val_if_fail (PALANG_IS_EDITOR (self), NULL);

  return self->search_bar;
}

void
palang_editor_show_search (PalangEditor *self,
                           gboolean      replace)
{
  g_return_if_fail (PALANG_IS_EDITOR (self));

  gtk_revealer_set_reveal_child (self->go_to_revealer, FALSE);
  palang_search_bar_open (self->search_bar, replace);
}

void
palang_editor_find_next (PalangEditor *self,
                         gboolean      forward)
{
  g_return_if_fail (PALANG_IS_EDITOR (self));

  if (palang_search_bar_is_open (self->search_bar))
    palang_search_bar_find (self->search_bar, forward);
  else
    palang_editor_show_search (self, FALSE);
}

/* ---- Go to line --------------------------------------------------------- */

/* Parses "42" or "42:10" into 1-based line and column numbers. */
static gboolean
parse_go_to_spec (const char *spec,
                  guint64    *line,
                  guint64    *column)
{
  /* g_strstrip trims spaces in place, so work on a copy. */
  g_autofree char *trimmed = g_strstrip (g_strdup (spec));
  g_auto (GStrv) parts = g_strsplit (trimmed, ":", 2);

  *line = 0;
  *column = 1;

  if (parts[0] == NULL ||
      !g_ascii_string_to_unsigned (parts[0], 10, 1, G_MAXINT, line, NULL))
    return FALSE;

  if (parts[1] != NULL &&
      !g_ascii_string_to_unsigned (parts[1], 10, 1, G_MAXINT, column, NULL))
    return FALSE;

  return TRUE;
}

gboolean
palang_editor_go_to (PalangEditor *self,
                     const char   *spec)
{
  GtkTextBuffer *buffer;
  GtkTextIter iter, line_end;
  guint64 line, column;

  g_return_val_if_fail (PALANG_IS_EDITOR (self), FALSE);
  g_return_val_if_fail (spec != NULL, FALSE);

  if (!parse_go_to_spec (spec, &line, &column))
    return FALSE;

  buffer = GTK_TEXT_BUFFER (self->document);

  /* Lines and offsets are 0-based in GtkTextBuffer. A line past the end
   * goes to the last line. */
  gtk_text_buffer_get_iter_at_line (buffer, &iter, (int) line - 1);

  /* A column past the end of the line goes to the end of the line. */
  line_end = iter;
  if (!gtk_text_iter_ends_line (&line_end))
    gtk_text_iter_forward_to_line_end (&line_end);
  gtk_text_iter_set_line_offset (&iter, MIN ((int) column - 1,
                                             gtk_text_iter_get_line_offset (&line_end)));

  gtk_text_buffer_place_cursor (buffer, &iter);
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (self->view),
                                gtk_text_buffer_get_insert (buffer),
                                0, TRUE, 0, 0.3);
  return TRUE;
}

void
palang_editor_show_go_to (PalangEditor *self)
{
  g_return_if_fail (PALANG_IS_EDITOR (self));

  palang_search_bar_close (self->search_bar);
  gtk_editable_set_text (GTK_EDITABLE (self->go_to_entry), "");
  gtk_widget_remove_css_class (GTK_WIDGET (self->go_to_entry), "error");
  gtk_revealer_set_reveal_child (self->go_to_revealer, TRUE);
  gtk_widget_grab_focus (GTK_WIDGET (self->go_to_entry));
}

static void
close_go_to (PalangEditor *self)
{
  gtk_revealer_set_reveal_child (self->go_to_revealer, FALSE);
  gtk_widget_grab_focus (GTK_WIDGET (self->view));
}

/* Enter in the "go to line" box. */
static void
on_go_to_activate (GtkEntry     *entry,
                   PalangEditor *self)
{
  if (palang_editor_go_to (self, gtk_editable_get_text (GTK_EDITABLE (entry))))
    close_go_to (self);
  else
    gtk_widget_add_css_class (GTK_WIDGET (entry), "error");
}

static void
on_go_to_close_clicked (GtkButton    *button,
                        PalangEditor *self)
{
  close_go_to (self);
}

/* Esc: close whichever bar is open. Returning FALSE lets Esc continue to
 * other handlers when there's nothing to close. */
static gboolean
escape_binding (GtkWidget *widget,
                GVariant  *args,
                gpointer   user_data)
{
  PalangEditor *self = PALANG_EDITOR (widget);

  if (gtk_revealer_get_reveal_child (self->go_to_revealer))
    {
      close_go_to (self);
      return TRUE;
    }

  return FALSE;
}

/* Clicking or tabbing into the editor focuses the text view. */
static gboolean
palang_editor_grab_focus (GtkWidget *widget)
{
  return gtk_widget_grab_focus (GTK_WIDGET (PALANG_EDITOR (widget)->view));
}

/* Settings → view: wrap-lines (a boolean) becomes a GtkWrapMode. */
static gboolean
wrap_lines_to_wrap_mode (GBinding     *binding,
                         const GValue *from_value,
                         GValue       *to_value,
                         gpointer      user_data)
{
  g_value_set_enum (to_value, g_value_get_boolean (from_value) ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
  return TRUE;
}

/* Makes the view follow the user's settings, now and when they change. */
static void
bind_settings (PalangEditor *self)
{
  PalangSettings *settings = palang_settings_get_default ();

  g_object_bind_property (settings, "show-line-numbers", self->view, "show-line-numbers",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "highlight-current-line", self->view, "highlight-current-line",
                          G_BINDING_SYNC_CREATE);
  /* The view's indent-width is -1 by default, meaning "same as tab-width". */
  g_object_bind_property (settings, "tab-width", self->view, "tab-width",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "insert-spaces", self->view, "insert-spaces-instead-of-tabs",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property_full (settings, "wrap-lines", self->view, "wrap-mode",
                               G_BINDING_SYNC_CREATE,
                               wrap_lines_to_wrap_mode, NULL, NULL, NULL);
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

  bind_settings (self);
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
  gtk_widget_class_bind_template_child (widget_class, PalangEditor, go_to_revealer);
  gtk_widget_class_bind_template_child (widget_class, PalangEditor, go_to_entry);
  gtk_widget_class_bind_template_callback (widget_class, on_go_to_activate);
  gtk_widget_class_bind_template_callback (widget_class, on_go_to_close_clicked);

  gtk_widget_class_add_binding (widget_class, GDK_KEY_Escape, 0, escape_binding, NULL);
}

static void
palang_editor_init (PalangEditor *self)
{
  /* The template's top-level box is the AdwBin's child (not bound as a
   * template child, so it isn't freed twice on dispose). */
  GtkWidget *box;

  gtk_widget_init_template (GTK_WIDGET (self));

  box = adw_bin_get_child (ADW_BIN (self));
  self->search_bar = palang_search_bar_new (self->view);
  gtk_box_prepend (GTK_BOX (box), GTK_WIDGET (self->search_bar));
}

PalangEditor *
palang_editor_new (PalangDocument *document)
{
  g_return_val_if_fail (PALANG_IS_DOCUMENT (document), NULL);

  return g_object_new (PALANG_TYPE_EDITOR,
                       "document", document,
                       NULL);
}
