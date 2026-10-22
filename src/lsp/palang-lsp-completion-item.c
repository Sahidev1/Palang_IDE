/* palang-lsp-completion-item.c — one entry of a completion list (see the
 * header).
 *
 * Reading the CompletionItem: every field except "label" is optional, so
 * they are read with g_variant_lookup (), which leaves the variable alone
 * when the key is missing or holds another type. Strings are copied: the
 * item outlives the server's message.
 *
 * Some servers send only a short version of each entry and the rest
 * (documentation, sometimes the detail) on request ("completionItem/
 * resolve", with the entry exactly as it came: get_raw ()). The answer
 * is taken in with apply_resolved ().
 */

#include "config.h"

#include <jsonrpc-glib.h>

#include "palang-lsp-completion-item.h"
#include "palang-lsp-markdown.h"
#include "palang-lsp-position.h"
#include "palang-lsp-snippet.h"

struct _PalangLspCompletionItem
{
  GObject parent_instance;

  char     *label;             /* owned */
  char     *filter_text;       /* owned */
  char     *sort_text;         /* owned */
  char     *detail;            /* owned, nullable */
  gint64    kind;              /* LSP CompletionItemKind, 0 if not given */
  char     *new_text;          /* owned: as the server wrote it */
  gboolean  is_snippet;        /* new_text is in snippet syntax */
  char     *plain_text;        /* owned: what new_text inserts as text */
  char     *documentation;     /* owned, nullable: Markdown */
  GVariant *raw;               /* owned: the CompletionItem as it came */
  gboolean  resolved;          /* apply_resolved () was called */
  gboolean  has_edit_start;
  guint     edit_line;
  guint     edit_character;
  GVariant *additional_edits;  /* owned, nullable */
  guint     score;             /* of the last match (), G_MAXUINT: no match */
  gboolean  exact_prefix;      /* the last match ()'s text starts it, case too */
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

/* "documentation": plain text, or MarkupContent {"kind", "value"}.
 * Returns: (transfer full) (nullable): it as Markdown. */
static char *
read_documentation (GVariant *item)
{
  g_autoptr (GVariant) documentation = g_variant_lookup_value (item, "documentation", NULL);
  const char *kind = NULL;
  const char *value = NULL;

  if (documentation == NULL)
    return NULL;

  if (g_variant_is_of_type (documentation, G_VARIANT_TYPE_STRING))
    value = g_variant_get_string (documentation, NULL);
  else if (!g_variant_is_of_type (documentation, G_VARIANT_TYPE_VARDICT) ||
           !JSONRPC_MESSAGE_PARSE (documentation,
                                   "kind", JSONRPC_MESSAGE_GET_STRING (&kind),
                                   "value", JSONRPC_MESSAGE_GET_STRING (&value)))
    return NULL;

  if (value[0] == '\0')
    return NULL;
  if (g_strcmp0 (kind, "markdown") == 0)
    return g_strdup (value);
  return palang_lsp_markdown_escape (value);  /* plain text shows as it is */
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
  gint64 format = 1;

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
  self->documentation = read_documentation (item);
  self->raw = g_variant_ref (item);
  g_variant_lookup (item, "kind", "x", &self->kind);

  /* What to insert: the textEdit's text, else insertText, else the
   * label. insertTextFormat 2 means it is a snippet. */
  self->new_text = dup_string (item, "insertText");
  if (self->new_text == NULL)
    self->new_text = g_strdup (self->label);
  read_text_edit (self, item);

  if (g_variant_lookup (item, "insertTextFormat", "x", &format) && format == 2)
    self->is_snippet = TRUE;
  self->plain_text = self->is_snippet ? palang_lsp_snippet_to_text (self->new_text)
                                      : g_strdup (self->new_text);

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

  return self->plain_text;
}

const char *
palang_lsp_completion_item_get_documentation (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), NULL);

  return self->documentation;
}

GVariant *
palang_lsp_completion_item_get_raw (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), NULL);

  return self->raw;
}

gboolean
palang_lsp_completion_item_is_resolved (PalangLspCompletionItem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), FALSE);

  return self->resolved;
}

void
palang_lsp_completion_item_apply_resolved (PalangLspCompletionItem *self,
                                           GVariant                *resolved)
{
  char *detail;
  char *documentation;
  GVariant *edits;

  g_return_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self));

  self->resolved = TRUE;
  if (resolved == NULL || !g_variant_is_of_type (resolved, G_VARIANT_TYPE_VARDICT))
    return;

  /* Only what the answer has replaces what we had. */
  detail = dup_string (resolved, "detail");
  if (detail != NULL)
    {
      g_free (self->detail);
      self->detail = detail;
    }

  documentation = read_documentation (resolved);
  if (documentation != NULL)
    {
      g_free (self->documentation);
      self->documentation = documentation;
    }

  edits = g_variant_lookup_value (resolved, "additionalTextEdits", G_VARIANT_TYPE ("av"));
  if (edits != NULL)
    {
      g_clear_pointer (&self->additional_edits, g_variant_unref);
      self->additional_edits = edits;
    }
}

gboolean
palang_lsp_completion_item_match (PalangLspCompletionItem *self,
                                  const char              *typed)
{
  g_autofree char *casefold_typed = NULL;

  g_return_val_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self), FALSE);
  g_return_val_if_fail (typed != NULL, FALSE);

  /* The matcher wants the typed text casefolded (case ignored); with the
   * case as typed, compare () puts entries starting with it first. */
  casefold_typed = g_utf8_casefold (typed, -1);
  self->exact_prefix = g_str_has_prefix (self->filter_text, typed);

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

  /* An entry starting with exactly what was typed comes first: the
   * matcher's score favours lowercase ("PRIx8" scores better than
   * "PRIX8" for "PRIX8"), and a real prefix beats a scattered match. */
  if (a->exact_prefix != b->exact_prefix)
    return a->exact_prefix ? -1 : 1;
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

/* Returns: (transfer full): a GtkSourceSnippet for @snippet (LSP
 * syntax), built chunk by chunk from palang_lsp_snippet_parse (). A chunk
 * is a piece of text, or a place to fill in ("focus position" 1, 2, …;
 * 0 is where the cursor ends; -1 means plain text). */
static GtkSourceSnippet *
new_source_snippet (const char *snippet)
{
  g_autoptr (GPtrArray) parts = palang_lsp_snippet_parse (snippet);
  GtkSourceSnippet *source_snippet = gtk_source_snippet_new (NULL, NULL);
  gboolean has_end = FALSE;

  for (guint i = 0; i < parts->len; i++)
    if (((PalangLspSnippetPart *) g_ptr_array_index (parts, i))->tab_stop == 0)
      has_end = TRUE;

  /* Without a $0, LSP puts the cursor at the end of the snippet after the
   * last place to fill in; GtkSourceView needs an empty chunk there to do
   * that (otherwise the last Tab inserts a tab). */
  if (!has_end)
    {
      PalangLspSnippetPart *end = g_new0 (PalangLspSnippetPart, 1);

      end->tab_stop = 0;
      end->text = g_strdup ("");
      g_ptr_array_add (parts, end);
    }

  for (guint i = 0; i < parts->len; i++)
    {
      PalangLspSnippetPart *part = g_ptr_array_index (parts, i);
      GtkSourceSnippetChunk *chunk = gtk_source_snippet_chunk_new ();

      /* "text-set": take the text as it is, don't expand it as
       * GtkSourceView's own snippet syntax (where "$1" means something). */
      gtk_source_snippet_chunk_set_text (chunk, part->text);
      gtk_source_snippet_chunk_set_text_set (chunk, TRUE);
      gtk_source_snippet_chunk_set_focus_position (chunk, part->tab_stop);
      gtk_source_snippet_add_chunk (source_snippet, chunk);  /* takes the floating ref */
    }

  return source_snippet;
}

void
palang_lsp_completion_item_insert (PalangLspCompletionItem *self,
                                   GtkSourceView           *view,
                                   const GtkTextIter       *word_start,
                                   const GtkTextIter       *cursor)
{
  GtkTextBuffer *buffer;
  GtkTextIter begin = *word_start;
  GtkTextIter end = *cursor;
  GtkTextIter edit_start;
  GtkTextMark *start_mark, *end_mark;

  g_return_if_fail (PALANG_IS_LSP_COMPLETION_ITEM (self));
  g_return_if_fail (GTK_SOURCE_IS_VIEW (view));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

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

  /* A snippet goes in through the view, which then moves from one place
   * to fill in to the next with Tab. */
  if (self->is_snippet)
    {
      g_autoptr (GtkSourceSnippet) snippet = new_source_snippet (self->new_text);

      gtk_source_view_push_snippet (view, snippet, &begin);
    }
  else
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
  g_free (self->plain_text);
  g_free (self->documentation);
  g_clear_pointer (&self->raw, g_variant_unref);
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
