/* palang-lsp-diagnostics.c — a server's errors and warnings in a buffer.
 *
 * Each diagnostic is kept as two GtkTextMarks (its start and end) plus its
 * message. Marks are positions that GTK moves along when text is inserted
 * or deleted before them, so the ranges stay on the right text while the
 * user types. The underline is a GtkTextTag over the same range, and the
 * gutter icon a GtkSourceMark (a mark with a "category" that views can
 * draw in the gutter).
 */

#include "config.h"

#include <jsonrpc-glib.h>

#include "palang-lsp-diagnostics.h"
#include "palang-lsp-position.h"

/* How each severity looks. The names serve as tag names, gutter mark
 * categories and keys for the views' mark attributes. Colors are GNOME's
 * palette (red 3, yellow 5, blue 3); they read well on light and dark. */
typedef struct {
  const char *name;
  const char *color;
  const char *icon_name;
  int         priority;  /* which icon wins when a line has several */
} Style;

static const Style styles[] = {
  { "palang-lsp-error",   "#e01b24", "dialog-error-symbolic",       30 },
  { "palang-lsp-warning", "#e5a50a", "dialog-warning-symbolic",     20 },
  { "palang-lsp-info",    "#3584e4", "dialog-information-symbolic", 10 },
};

typedef struct {
  GtkTextMark *start;  /* owned by the buffer; deleted in clear */
  GtkTextMark *end;
  char        *text;   /* "Error: … (clang)" */
} Diagnostic;

struct _PalangLspDiagnostics
{
  GtkSourceBuffer *buffer;       /* not owned */
  GArray          *diagnostics;  /* of Diagnostic */
};

/* The protocol's severities: 1 error, 2 warning, 3 information, 4 hint.
 * A missing severity is up to the client; we treat it as an error. */
static const Style *
style_for_severity (gint64 severity)
{
  if (severity == 2)
    return &styles[1];
  if (severity == 3 || severity == 4)
    return &styles[2];
  return &styles[0];
}

static const char *
label_for_severity (gint64 severity)
{
  switch (severity)
    {
    case 2:  return "Warning";
    case 3:  return "Info";
    case 4:  return "Hint";
    default: return "Error";
    }
}

/* Returns: (transfer none): the buffer's tag for @style, created the first
 * time it is needed. */
static GtkTextTag *
get_tag (PalangLspDiagnostics *self,
         const Style          *style)
{
  GtkTextTagTable *table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (self->buffer));
  GtkTextTag *tag = gtk_text_tag_table_lookup (table, style->name);
  GdkRGBA color;

  if (tag != NULL)
    return tag;

  gdk_rgba_parse (&color, style->color);
  /* PANGO_UNDERLINE_ERROR is the wavy "spelling mistake" underline. */
  return gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (self->buffer), style->name,
                                     "underline", PANGO_UNDERLINE_ERROR,
                                     "underline-rgba", &color,
                                     NULL);
}

/* An empty range (some servers mark a point, e.g. "missing ';'") would
 * show nothing: widen it to the next character, or the previous one at
 * the end of a line. */
static void
widen_empty_range (GtkTextIter *start,
                   GtkTextIter *end)
{
  if (!gtk_text_iter_equal (start, end))
    return;

  if (!gtk_text_iter_ends_line (end))
    gtk_text_iter_forward_char (end);
  else if (!gtk_text_iter_starts_line (start))
    gtk_text_iter_backward_char (start);
}

static void
add_diagnostic (PalangLspDiagnostics *self,
                GVariant             *diagnostic)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (self->buffer);
  gint64 start_line = 0, start_character = 0, end_line = 0, end_character = 0;
  gint64 severity = 1;
  const char *message = NULL;
  const char *source = NULL;
  const Style *style;
  GtkTextIter start, end, line_start;
  Diagnostic entry;

  /* JSONRPC_MESSAGE_PARSE is the reading twin of JSONRPC_MESSAGE_NEW: it
   * fills in the variables and returns FALSE if something is missing. */
  if (!JSONRPC_MESSAGE_PARSE (diagnostic,
        "range", "{",
          "start", "{",
            "line", JSONRPC_MESSAGE_GET_INT64 (&start_line),
            "character", JSONRPC_MESSAGE_GET_INT64 (&start_character),
          "}",
          "end", "{",
            "line", JSONRPC_MESSAGE_GET_INT64 (&end_line),
            "character", JSONRPC_MESSAGE_GET_INT64 (&end_character),
          "}",
        "}",
        "message", JSONRPC_MESSAGE_GET_STRING (&message)))
    {
      g_debug ("Skipping a diagnostic without a range or message");
      return;
    }

  /* Optional members. */
  g_variant_lookup (diagnostic, "severity", "x", &severity);
  g_variant_lookup (diagnostic, "source", "&s", &source);
  style = style_for_severity (severity);

  palang_lsp_iter_from_position (buffer, (guint) MAX (start_line, 0),
                                 (guint) MAX (start_character, 0), &start);
  palang_lsp_iter_from_position (buffer, (guint) MAX (end_line, 0),
                                 (guint) MAX (end_character, 0), &end);
  gtk_text_iter_order (&start, &end);
  widen_empty_range (&start, &end);

  gtk_text_buffer_apply_tag (buffer, get_tag (self, style), &start, &end);

  /* The gutter icon goes at the start of the first line. */
  line_start = start;
  gtk_text_iter_set_line_offset (&line_start, 0);
  gtk_source_buffer_create_source_mark (self->buffer, NULL, style->name, &line_start);

  /* "Left gravity" decides which side of text typed exactly at the mark
   * the mark ends up on. These keep text typed at either edge outside. */
  entry.start = gtk_text_buffer_create_mark (buffer, NULL, &start, FALSE);
  entry.end = gtk_text_buffer_create_mark (buffer, NULL, &end, TRUE);
  if (source != NULL && source[0] != '\0')
    entry.text = g_strdup_printf ("%s: %s (%s)", label_for_severity (severity), message, source);
  else
    entry.text = g_strdup_printf ("%s: %s", label_for_severity (severity), message);
  g_array_append_val (self->diagnostics, entry);
}

void
palang_lsp_diagnostics_set (PalangLspDiagnostics *self,
                            GVariant             *diagnostics)
{
  g_return_if_fail (self != NULL);

  palang_lsp_diagnostics_clear (self);

  if (diagnostics == NULL || !g_variant_is_container (diagnostics))
    return;

  for (gsize i = 0; i < g_variant_n_children (diagnostics); i++)
    {
      g_autoptr (GVariant) child = g_variant_get_child_value (diagnostics, i);

      /* jsonrpc-glib gives JSON arrays as "av": each element is boxed in
       * a variant. Unbox it. */
      if (g_variant_is_of_type (child, G_VARIANT_TYPE_VARIANT))
        {
          GVariant *unboxed = g_variant_get_variant (child);

          g_variant_unref (child);
          child = unboxed;
        }

      if (g_variant_is_of_type (child, G_VARIANT_TYPE_VARDICT))
        add_diagnostic (self, child);
    }
}

void
palang_lsp_diagnostics_clear (PalangLspDiagnostics *self)
{
  GtkTextBuffer *buffer;
  GtkTextTagTable *table;
  GtkTextIter start, end;

  g_return_if_fail (self != NULL);

  buffer = GTK_TEXT_BUFFER (self->buffer);
  table = gtk_text_buffer_get_tag_table (buffer);
  gtk_text_buffer_get_bounds (buffer, &start, &end);

  for (guint i = 0; i < G_N_ELEMENTS (styles); i++)
    {
      GtkTextTag *tag = gtk_text_tag_table_lookup (table, styles[i].name);

      if (tag != NULL)
        gtk_text_buffer_remove_tag (buffer, tag, &start, &end);
      gtk_source_buffer_remove_source_marks (self->buffer, &start, &end, styles[i].name);
    }

  for (guint i = 0; i < self->diagnostics->len; i++)
    {
      Diagnostic *entry = &g_array_index (self->diagnostics, Diagnostic, i);

      gtk_text_buffer_delete_mark (buffer, entry->start);
      gtk_text_buffer_delete_mark (buffer, entry->end);
      g_free (entry->text);
    }
  g_array_set_size (self->diagnostics, 0);
}

guint
palang_lsp_diagnostics_get_count (PalangLspDiagnostics *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return self->diagnostics->len;
}

char **
palang_lsp_diagnostics_dup_messages_at (PalangLspDiagnostics *self,
                                        const GtkTextIter    *iter)
{
  GtkTextBuffer *buffer;
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  guint n_found = 0;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (iter != NULL, NULL);

  buffer = GTK_TEXT_BUFFER (self->buffer);
  for (guint i = 0; i < self->diagnostics->len; i++)
    {
      Diagnostic *entry = &g_array_index (self->diagnostics, Diagnostic, i);
      GtkTextIter start, end;

      gtk_text_buffer_get_iter_at_mark (buffer, &start, entry->start);
      gtk_text_buffer_get_iter_at_mark (buffer, &end, entry->end);
      /* The end counts too: GtkSourceHover gives the character boundary
       * *nearest* to the pointer, so over the right half of a one-letter
       * range ("x") the iter is already past it. */
      if (gtk_text_iter_compare (&start, iter) <= 0 && gtk_text_iter_compare (iter, &end) <= 0)
        {
          g_strv_builder_add (builder, entry->text);
          n_found++;
        }
    }

  if (n_found == 0)
    return NULL;

  return g_strv_builder_end (builder);
}

void
palang_lsp_diagnostics_setup_view (GtkSourceView *view)
{
  g_return_if_fail (GTK_SOURCE_IS_VIEW (view));

  for (guint i = 0; i < G_N_ELEMENTS (styles); i++)
    {
      g_autoptr (GtkSourceMarkAttributes) attributes = gtk_source_mark_attributes_new ();

      gtk_source_mark_attributes_set_icon_name (attributes, styles[i].icon_name);
      gtk_source_view_set_mark_attributes (view, styles[i].name, attributes, styles[i].priority);
    }

  /* The gutter column for marks. It is always there (also without any
   * diagnostics), so the text doesn't jump sideways when one appears. */
  gtk_source_view_set_show_line_marks (view, TRUE);
}

PalangLspDiagnostics *
palang_lsp_diagnostics_new (GtkSourceBuffer *buffer)
{
  PalangLspDiagnostics *self;

  g_return_val_if_fail (GTK_SOURCE_IS_BUFFER (buffer), NULL);

  self = g_new0 (PalangLspDiagnostics, 1);
  self->buffer = buffer;
  self->diagnostics = g_array_new (FALSE, FALSE, sizeof (Diagnostic));
  return self;
}

void
palang_lsp_diagnostics_free (PalangLspDiagnostics *self)
{
  if (self == NULL)
    return;

  palang_lsp_diagnostics_clear (self);
  g_array_unref (self->diagnostics);
  g_free (self);
}
