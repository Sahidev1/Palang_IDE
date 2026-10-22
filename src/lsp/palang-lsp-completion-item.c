/* palang-lsp-completion-item.c — one entry of a completion list (see the
 * header).
 *
 * Reading the CompletionItem: every field except "label" is optional, so
 * they are read with g_variant_lookup (), which leaves the variable alone
 * when the key is missing or holds another type. Strings are copied: the
 * item outlives the server's message.
 */

#include "config.h"

#include <jsonrpc-glib.h>

#include "palang-lsp-completion-item.h"
#include "palang-lsp-position.h"

struct _PalangLspCompletionItem
{
  GObject parent_instance;

  char     *label;             /* owned */
  char     *filter_text;       /* owned */
  char     *sort_text;         /* owned */
  char     *detail;            /* owned, nullable */
  gint64    kind;              /* LSP CompletionItemKind, 0 if not given */
  char     *new_text;          /* owned */
  gboolean  has_edit_start;
  guint     edit_line;
  guint     edit_character;
  GVariant *additional_edits;  /* owned, nullable */
  guint     score;             /* of the last match (), G_MAXUINT: no match */
};

/* The interface has no required functions; G_IMPLEMENT_INTERFACE with
 * NULL just records that we provide it. */
G_DEFINE_FINAL_TYPE_WITH_CODE (PalangLspCompletionItem, palang_lsp_completion_item, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (GTK_SOURCE_TYPE_COMPLETION_PROPOSAL, NULL))

/* The protocol's CompletionItemKind numbers, as words. */
static const char * const kind_names[] = {
  NULL, "Text", "Method", "Function", "Constructor", "Field", "Variable",
  "Class", "Interface", "Module", "Property", "Unit", "Value", "Enum",
  "Keyword", "Snippet", "Color", "File", "Reference", "Folder",
  "Enum Member", "Constant", "Struct", "Event", "Operator", "Type Parameter",
};

/* Returns: (transfer full) (nullable): a copy of the string at @key. */
static char *
dup_string (GVariant   *item,
            const char *key)
{
  const char *value = NULL;

  if (!g_variant_lookup (item, key, "&s", &value))
    return NULL;
  return g_strdup (value);
}

/* "textEdit": {"range": {"start": …, "end": …}, "newText": "…"}. Takes
 * the new text and where it starts. */
static void
read_text_edit (PalangLspCompletionItem *self,
                GVariant                *item)
{
  g_autoptr (GVariant) edit = g_variant_lookup_value (item, "textEdit", G_VARIANT_TYPE_VARDICT);
  const char *new_text = NULL;
  gint64 line = 0, character = 0;

  if (edit == NULL)
    return;

  if (!JSONRPC_MESSAGE_PARSE (edit,
                              "newText", JSONRPC_MESSAGE_GET_STRING (&new_text),
                              "range", "{",
                                "start", "{",
                                  "line", JSONRPC_MESSAGE_GET_INT64 (&line),
                                  "character", JSONRPC_MESSAGE_GET_INT64 (&character),
                                "}",
                              "}"))
    return;

  g_free (self->new_text);
  self->new_text = g_strdup (new_text);
  self->has_edit_start = TRUE;
  self->edit_line = (guint) MAX (line, 0);
  self->edit_character = (guint) MAX (character, 0);
}

PalangLspCompletionItem *
palang_lsp_completion_item_new (GVariant *item)
{
  PalangLspCompletionItem *self;
  const char *label = NULL;

  g_return_val_if_fail (item != NULL, NULL);

  if (!g_variant_is_of_type (item, G_VARIANT_TYPE_VARDICT) ||
      !g_variant_lookup (item, "label", "&s", &label))
    return NULL;

  self = g_object_new (PALANG_TYPE_LSP_COMPLETION_ITEM, NULL);

  /* clangd starts labels with a space (or "•" when it will add an
   * #include) to line them up; the space only takes room here. */
  while (*label == ' ')
    label++;
  self->label = g_strdup (label);

  self->filter_text = dup_string (item, "filterText");
  if (self->filter_text == NULL)
    self->filter_text = g_strdup (self->label);

  self->sort_text = dup_string (item, "sortText");
  if (self->sort_text == NULL)
    self->sort_text = g_strdup (self->label);

  self->detail = dup_string (item, "detail");
  g_variant_lookup (item, "kind", "x", &self->kind);

  /* What to insert: the textEdit's text, else insertText, else the
   * label. */
  self->new_text = dup_string (item, "insertText");
  if (self->new_text == NULL)
    self->new_text = g_strdup (self->label);
  read_text_edit (self, item);

  self->additional_edits = g_variant_lookup_value (item, "additionalTextEdits",
                                                   G_VARIANT_TYPE ("av"));
  return self;
}

const char *
palang_lsp_completion_item_get_label (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), NULL);

  return self->label;
}

const char *
palang_lsp_completion_item_get_filter_text (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), NULL);

  return self->filter_text;
}

const char *
palang_lsp_completion_item_get_sort_text (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), NULL);

  return self->sort_text;
}

const char *
palang_lsp_completion_item_get_detail (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), NULL);

  return self->detail;
}

const char *
palang_lsp_completion_item_get_kind_name (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), NULL);

  if (self->kind < 1 || self->kind >= (gint64) G_N_ELEMENTS (kind_names))
    return NULL;
  return kind_names[self->kind];
}

const char *
palang_lsp_completion_item_get_new_text (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), NULL);

  return self->new_text;
}

gboolean
palang_lsp_completion_item_match (PalangLspCompletionItem *self,
                                  const char              *casefold_typed)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), FALSE);

  /* GtkSourceView's fuzzy matcher: the typed letters in order, not
   * necessarily next to each other ("gws" matches "gtk_widget_show");
   * the score is lower for closer matches. */
  if (casefold_typed[0] == '\0')
    self->score = 0;
  else if (!gtk_source_completion_fuzzy_match (self->filter_text, casefold_typed, &self->score))
    self->score = G_MAXUINT;

  return self->score != G_MAXUINT;
}

guint
palang_lsp_completion_item_get_score (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), G_MAXUINT);

  return self->score;
}

int
palang_lsp_completion_item_compare (PalangLspCompletionItem *a,
                                    PalangLspCompletionItem *b)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (a), 0);
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (b), 0);

  if (a->score != b->score)
    return a->score < b->score ? -1 : 1;

  return g_strcmp0 (a->sort_text, b->sort_text);
}

/* Makes one LSP TextEdit, {"range": {start, end}, "newText": …}: the
 * text in the range is replaced with the new text. */
static void
apply_text_edit (GtkTextBuffer *buffer,
                 GVariant      *edit)
{
  const char *new_text = NULL;
  gint64 start_line = 0, start_character = 0, end_line = 0, end_character = 0;
  GtkTextIter start, end;

  if (!JSONRPC_MESSAGE_PARSE (edit,
                              "newText", JSONRPC_MESSAGE_GET_STRING (&new_text),
                              "range", "{",
                                "start", "{",
                                  "line", JSONRPC_MESSAGE_GET_INT64 (&start_line),
                                  "character", JSONRPC_MESSAGE_GET_INT64 (&start_character),
                                "}",
                                "end", "{",
                                  "line", JSONRPC_MESSAGE_GET_INT64 (&end_line),
                                  "character", JSONRPC_MESSAGE_GET_INT64 (&end_character),
                                "}",
                              "}"))
    return;

  palang_lsp_iter_from_position (buffer, (guint) MAX (start_line, 0),
                                 (guint) MAX (start_character, 0), &start);
  palang_lsp_iter_from_position (buffer, (guint) MAX (end_line, 0),
                                 (guint) MAX (end_character, 0), &end);
  gtk_text_buffer_delete (buffer, &start, &end);
  gtk_text_buffer_insert (buffer, &start, new_text, -1);
}

void
palang_lsp_completion_item_insert (PalangLspCompletionItem *self,
                                   GtkTextBuffer           *buffer,
                                   const GtkTextIter       *word_start,
                                   const GtkTextIter       *cursor)
{
  GtkTextIter begin = *word_start;
  GtkTextIter end = *cursor;
  GtkTextIter edit_start;
  GtkTextMark *start_mark, *end_mark;

  g_return_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self));
  g_return_if_fail (GTK_IS_TEXT_BUFFER (buffer));

  /* The server may say where the replaced text starts (clangd does);
   * else it is the typed word. Only a start on the same line, before the
   * cursor, makes sense. */
  if (self->has_edit_start)
    {
      palang_lsp_iter_from_position (buffer, self->edit_line, self->edit_character, &edit_start);
      if (gtk_text_iter_get_line (&edit_start) == gtk_text_iter_get_line (&end) &&
          gtk_text_iter_compare (&edit_start, &end) <= 0)
        begin = edit_start;
    }

  gtk_text_buffer_begin_user_action (buffer);

  /* Marks keep their place when text before them changes, iters don't:
   * the other edits (usually above, at the #includes) would move the
   * word. */
  start_mark = gtk_text_buffer_create_mark (buffer, NULL, &begin, TRUE);
  end_mark = gtk_text_buffer_create_mark (buffer, NULL, &end, FALSE);

  /* From last to first, so earlier positions stay right. */
  for (gsize i = self->additional_edits != NULL ? g_variant_n_children (self->additional_edits) : 0;
       i > 0; i--)
    {
      g_autoptr (GVariant) boxed = g_variant_get_child_value (self->additional_edits, i - 1);
      g_autoptr (GVariant) edit = g_variant_get_variant (boxed);

      if (g_variant_is_of_type (edit, G_VARIANT_TYPE_VARDICT))
        apply_text_edit (buffer, edit);
    }

  gtk_text_buffer_get_iter_at_mark (buffer, &begin, start_mark);
  gtk_text_buffer_get_iter_at_mark (buffer, &end, end_mark);
  gtk_text_buffer_delete (buffer, &begin, &end);
  gtk_text_buffer_insert (buffer, &begin, self->new_text, -1);

  gtk_text_buffer_delete_mark (buffer, start_mark);
  gtk_text_buffer_delete_mark (buffer, end_mark);
  gtk_text_buffer_end_user_action (buffer);
}

static void
palang_lsp_completion_item_finalize (GObject *object)
{
  PalangLspCompletionItem *self = PALANG_LSP_COMPLETION_ITEM (object);

  g_free (self->label);
  g_free (self->filter_text);
  g_free (self->sort_text);
  g_free (self->detail);
  g_free (self->new_text);
  g_clear_pointer (&self->additional_edits, g_variant_unref);

  G_OBJECT_CLASS (palang_lsp_completion_item_parent_class)->finalize (object);
}

static void
palang_lsp_completion_item_class_init (PalangLspCompletionItemClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = palang_lsp_completion_item_finalize;
}

static void
palang_lsp_completion_item_init (PalangLspCompletionItem *self)
{
}
