/* palang-bracket-pairs.c — brackets and quotes that close themselves.
 *
 * Keys are caught by a key controller on the view's parent, in the capture
 * phase, so we see them before the view does (see on_key_pressed ()), and
 * handled by the public functions, which tests call directly.
 *
 * Enter between brackets ("{|}" → the closer on its own line, the cursor
 * on an indented line in between) is here too. It is always on, also with
 * auto-close-brackets off: it adds no character the user didn't type,
 * only lays out the lines.
 *
 * "Pending" pairs: a closer we added is remembered until the cursor leaves
 * the pair, so that typing it steps over it (and Backspace in an empty one
 * deletes both). A closer the user typed, or one in a file that was
 * opened, is never stepped over: typing ")" before it adds another ")".
 */

#include "config.h"

#include "palang-bracket-pairs.h"

/* One pair we added, e.g. "(foo|)". Two GtkTextMarks keep track of where
 * its inside starts and ends while the text around it changes. A mark is a
 * position in a GtkTextBuffer that moves with the text; its "gravity"
 * says where it goes when text is inserted exactly at it: a left-gravity
 * mark stays before the new text, a right-gravity one ends up after it. So
 * typing inside the pair leaves `inside_start` before the typed text and
 * pushes `inside_end` along, and `inside_end` stays right before the
 * closer. */
typedef struct {
  GtkTextMark *inside_start;  /* owned; left gravity, after the opener */
  GtkTextMark *inside_end;    /* owned; right gravity, before the closer */
  gunichar     opener;
  gunichar     closer;
} PendingPair;

struct _PalangBracketPairs
{
  GObject parent_instance;

  GtkSourceView *view;     /* owned */
  GtkTextBuffer *buffer;   /* owned; the view's buffer */
  gboolean       enabled;

  GPtrArray *pending;      /* of PendingPair, innermost last */
};

G_DEFINE_FINAL_TYPE (PalangBracketPairs, palang_bracket_pairs, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_ENABLED,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ---- Pure helpers ------------------------------------------------------- */

gunichar
palang_bracket_pairs_closer_for (gunichar ch)
{
  switch (ch)
    {
    case '(':  return ')';
    case '[':  return ']';
    case '{':  return '}';
    case '"':  return '"';
    case '\'': return '\'';
    default:   return 0;
    }
}

static gboolean
is_word_character (gunichar ch)
{
  return g_unichar_isalnum (ch) || ch == '_';
}

gboolean
palang_bracket_pairs_should_close (gunichar opener,
                                   gunichar before,
                                   gunichar after)
{
  gboolean is_quote = opener == '"' || opener == '\'';

  if (palang_bracket_pairs_closer_for (opener) == 0)
    return FALSE;

  /* "(|foo": the user is about to wrap foo by hand, or call something. */
  if (after != 0 && is_word_character (after))
    return FALSE;

  /* "don|t", "x'|": a quote right after a word is an apostrophe, a
   * prime, or the end of a string, not the start of one. */
  if (is_quote && before != 0 && is_word_character (before))
    return FALSE;

  return TRUE;
}

/* ---- Pending pairs ------------------------------------------------------ */

static void
pending_pair_free (gpointer data)
{
  PendingPair *pair = data;
  GtkTextBuffer *buffer = gtk_text_mark_get_buffer (pair->inside_start);

  /* The buffer is NULL if the marks were already removed from it. */
  if (buffer != NULL)
    {
      gtk_text_buffer_delete_mark (buffer, pair->inside_start);
      gtk_text_buffer_delete_mark (buffer, pair->inside_end);
    }

  g_object_unref (pair->inside_start);
  g_object_unref (pair->inside_end);
  g_free (pair);
}

/* Remembers the pair around the cursor, which was just added. */
static void
add_pending (PalangBracketPairs *self,
             gunichar            opener,
             gunichar            closer)
{
  PendingPair *pair = g_new0 (PendingPair, 1);
  GtkTextIter cursor;

  gtk_text_buffer_get_iter_at_mark (self->buffer, &cursor, gtk_text_buffer_get_insert (self->buffer));

  /* gtk_text_buffer_create_mark () returns a mark owned by the buffer; we
   * keep our own reference so the pointer stays valid however long the
   * buffer lives. */
  pair->inside_start = g_object_ref (gtk_text_buffer_create_mark (self->buffer, NULL, &cursor, TRUE));
  pair->inside_end = g_object_ref (gtk_text_buffer_create_mark (self->buffer, NULL, &cursor, FALSE));
  pair->opener = opener;
  pair->closer = closer;

  g_ptr_array_add (self->pending, pair);
}

/* TRUE if the pair's closer is still there and the cursor is inside it. */
static gboolean
pair_is_still_around_cursor (PalangBracketPairs *self,
                             PendingPair        *pair)
{
  GtkTextIter start, end, cursor;

  gtk_text_buffer_get_iter_at_mark (self->buffer, &start, pair->inside_start);
  gtk_text_buffer_get_iter_at_mark (self->buffer, &end, pair->inside_end);
  gtk_text_buffer_get_iter_at_mark (self->buffer, &cursor, gtk_text_buffer_get_insert (self->buffer));

  return gtk_text_iter_get_char (&end) == pair->closer &&
         gtk_text_iter_compare (&start, &cursor) <= 0 &&
         gtk_text_iter_compare (&cursor, &end) <= 0;
}

/* The cursor moved (typing, clicking, arrow keys, undo…): forget the pairs
 * it left, and those whose closer was deleted. */
static void
on_cursor_moved (GtkTextBuffer      *buffer,
                 GParamSpec         *pspec,
                 PalangBracketPairs *self)
{
  /* Backwards, so removing an element doesn't skip the next one. */
  for (guint i = self->pending->len; i > 0; i--)
    if (!pair_is_still_around_cursor (self, g_ptr_array_index (self->pending, i - 1)))
      g_ptr_array_remove_index (self->pending, i - 1);
}

/* Returns: (nullable): the innermost pending pair whose closer is right at
 * the cursor. With @empty, only one with nothing inside: "(|)". */
static PendingPair *
find_pending_at_cursor (PalangBracketPairs *self,
                        gboolean            empty)
{
  GtkTextIter cursor;

  gtk_text_buffer_get_iter_at_mark (self->buffer, &cursor, gtk_text_buffer_get_insert (self->buffer));

  for (guint i = self->pending->len; i > 0; i--)
    {
      PendingPair *pair = g_ptr_array_index (self->pending, i - 1);
      GtkTextIter start, end;

      gtk_text_buffer_get_iter_at_mark (self->buffer, &start, pair->inside_start);
      gtk_text_buffer_get_iter_at_mark (self->buffer, &end, pair->inside_end);

      if (gtk_text_iter_equal (&end, &cursor) &&
          gtk_text_iter_get_char (&end) == pair->closer &&
          (!empty || gtk_text_iter_equal (&start, &end)))
        return pair;
    }

  return NULL;
}

/* ---- Typing ------------------------------------------------------------- */

/* Puts @opener and @closer around the selection, which stays selected. */
static void
wrap_selection (PalangBracketPairs *self,
                gunichar            opener,
                gunichar            closer)
{
  gboolean editable = gtk_text_view_get_editable (GTK_TEXT_VIEW (self->view));
  char opener_text[8] = { 0 };
  char closer_text[8] = { 0 };
  GtkTextIter start, end;
  GtkTextMark *end_mark;
  GtkTextMark *start_mark;

  g_unichar_to_utf8 (opener, opener_text);
  g_unichar_to_utf8 (closer, closer_text);

  gtk_text_buffer_get_selection_bounds (self->buffer, &start, &end);

  /* Inserting text invalidates every GtkTextIter of the buffer (except the
   * one passed in, which is moved to the end of the new text), so the far
   * end of the selection is kept in a mark meanwhile. Left gravity: the
   * closer inserted there goes after it, outside the selection. */
  end_mark = gtk_text_buffer_create_mark (self->buffer, NULL, &end, TRUE);

  /* A "user action" groups changes into one undo step. */
  gtk_text_buffer_begin_user_action (self->buffer);

  gtk_text_buffer_insert_interactive (self->buffer, &start, opener_text, -1, editable);
  start_mark = gtk_text_buffer_create_mark (self->buffer, NULL, &start, TRUE);

  gtk_text_buffer_get_iter_at_mark (self->buffer, &end, end_mark);
  gtk_text_buffer_insert_interactive (self->buffer, &end, closer_text, -1, editable);

  /* Select the original text again, now inside the pair. */
  gtk_text_buffer_get_iter_at_mark (self->buffer, &start, start_mark);
  gtk_text_buffer_get_iter_at_mark (self->buffer, &end, end_mark);
  gtk_text_buffer_select_range (self->buffer, &end, &start);

  gtk_text_buffer_end_user_action (self->buffer);

  gtk_text_buffer_delete_mark (self->buffer, start_mark);
  gtk_text_buffer_delete_mark (self->buffer, end_mark);
}

gboolean
palang_bracket_pairs_type_char (PalangBracketPairs *self,
                                gunichar            ch)
{
  gboolean editable;
  gunichar closer;
  PendingPair *pending;
  GtkTextIter cursor, previous;
  gunichar before;
  char text[16] = { 0 };
  int length;

  g_return_val_if_fail (PALANG_IS_BRACKET_PAIRS (self), FALSE);

  editable = gtk_text_view_get_editable (GTK_TEXT_VIEW (self->view));
  if (!self->enabled || !editable)
    return FALSE;

  /* A closer we added is right at the cursor: step over it. (For quotes
   * this comes first: '"' is both an opener and a closer.) */
  if (!gtk_text_buffer_get_has_selection (self->buffer))
    {
      pending = find_pending_at_cursor (self, FALSE);
      if (pending != NULL && pending->closer == ch)
        {
          gtk_text_buffer_get_iter_at_mark (self->buffer, &cursor, gtk_text_buffer_get_insert (self->buffer));
          gtk_text_iter_forward_char (&cursor);
          /* Moving the cursor past the closer also forgets the pair
           * (on_cursor_moved). */
          gtk_text_buffer_place_cursor (self->buffer, &cursor);
          return TRUE;
        }
    }

  closer = palang_bracket_pairs_closer_for (ch);
  if (closer == 0)
    return FALSE;

  if (gtk_text_buffer_get_has_selection (self->buffer))
    {
      wrap_selection (self, ch, closer);
      return TRUE;
    }

  gtk_text_buffer_get_iter_at_mark (self->buffer, &cursor, gtk_text_buffer_get_insert (self->buffer));
  previous = cursor;
  before = gtk_text_iter_backward_char (&previous) ? gtk_text_iter_get_char (&previous) : 0;
  /* gtk_text_iter_get_char () gives 0 at the end of the text. */
  if (!palang_bracket_pairs_should_close (ch, before, gtk_text_iter_get_char (&cursor)))
    return FALSE;

  /* "()" as UTF-8 (each character is 1 to 4 bytes). */
  length = g_unichar_to_utf8 (ch, text);
  g_unichar_to_utf8 (closer, text + length);

  gtk_text_buffer_begin_user_action (self->buffer);
  gtk_text_buffer_insert_interactive_at_cursor (self->buffer, text, -1, editable);
  /* The cursor is after the closer now; put it between the two. */
  gtk_text_buffer_get_iter_at_mark (self->buffer, &cursor, gtk_text_buffer_get_insert (self->buffer));
  gtk_text_iter_backward_char (&cursor);
  gtk_text_buffer_place_cursor (self->buffer, &cursor);
  gtk_text_buffer_end_user_action (self->buffer);

  add_pending (self, ch, closer);
  return TRUE;
}

gboolean
palang_bracket_pairs_backspace (PalangBracketPairs *self)
{
  PendingPair *pair;
  GtkTextIter start, end;

  g_return_val_if_fail (PALANG_IS_BRACKET_PAIRS (self), FALSE);

  if (!self->enabled || gtk_text_buffer_get_has_selection (self->buffer))
    return FALSE;

  pair = find_pending_at_cursor (self, TRUE);
  if (pair == NULL)
    return FALSE;

  /* "(|)": the opener before the cursor, the closer after it. */
  gtk_text_buffer_get_iter_at_mark (self->buffer, &start, pair->inside_start);
  end = start;
  if (!gtk_text_iter_backward_char (&start) || gtk_text_iter_get_char (&start) != pair->opener)
    return FALSE;
  gtk_text_iter_forward_char (&end);

  /* Deleting moves the cursor out of the pair, which forgets it. */
  gtk_text_buffer_begin_user_action (self->buffer);
  gtk_text_buffer_delete_interactive (self->buffer, &start, &end,
                                      gtk_text_view_get_editable (GTK_TEXT_VIEW (self->view)));
  gtk_text_buffer_end_user_action (self->buffer);
  return TRUE;
}

/* ---- Enter between brackets --------------------------------------------- */

/* Returns: (transfer full): the spaces and tabs at the start of @iter's
 * line, up to @iter at most. */
static char *
dup_line_indentation (const GtkTextIter *iter)
{
  GtkTextIter start = *iter;
  GtkTextIter end;

  gtk_text_iter_set_line_offset (&start, 0);
  end = start;
  while (gtk_text_iter_compare (&end, iter) < 0 &&
         (gtk_text_iter_get_char (&end) == ' ' || gtk_text_iter_get_char (&end) == '\t'))
    gtk_text_iter_forward_char (&end);

  return gtk_text_iter_get_slice (&start, &end);
}

/* Returns: (transfer full): one level of indentation, as Tab would insert
 * it: a tab, or as many spaces as the indent width (which the editor
 * binds to the tab-width and insert-spaces settings). */
static char *
dup_indent_unit (PalangBracketPairs *self)
{
  int width = gtk_source_view_get_indent_width (self->view);

  if (!gtk_source_view_get_insert_spaces_instead_of_tabs (self->view))
    return g_strdup ("\t");

  /* -1 means "the same as the tab width". */
  if (width <= 0)
    width = (int) gtk_source_view_get_tab_width (self->view);

  return g_strnfill (width, ' ');
}

gboolean
palang_bracket_pairs_newline (PalangBracketPairs *self)
{
  gboolean editable;
  GtkTextIter cursor, previous;
  gunichar opener;
  g_autofree char *indentation = NULL;
  g_autofree char *unit = NULL;
  g_autofree char *text = NULL;
  int cursor_offset;

  g_return_val_if_fail (PALANG_IS_BRACKET_PAIRS (self), FALSE);

  editable = gtk_text_view_get_editable (GTK_TEXT_VIEW (self->view));
  if (!editable || gtk_text_buffer_get_has_selection (self->buffer))
    return FALSE;

  /* Right between an opening bracket and its closer: "{|}". Quotes don't
   * count: a string doesn't get a block. */
  gtk_text_buffer_get_iter_at_mark (self->buffer, &cursor, gtk_text_buffer_get_insert (self->buffer));
  previous = cursor;
  if (!gtk_text_iter_backward_char (&previous))
    return FALSE;
  opener = gtk_text_iter_get_char (&previous);
  if (opener != '(' && opener != '[' && opener != '{')
    return FALSE;
  if (gtk_text_iter_get_char (&cursor) != palang_bracket_pairs_closer_for (opener))
    return FALSE;

  /* "    if (x) {|}" becomes
   *     "    if (x) {\n"
   *     "        |\n"
   *     "    }"
   * The closer keeps the opening line's indentation; the new line in
   * between gets one level more. */
  indentation = dup_line_indentation (&cursor);
  unit = dup_indent_unit (self);
  text = g_strconcat ("\n", indentation, unit, "\n", indentation, NULL);

  /* Where the cursor goes: the end of the middle line. Offsets count
   * characters from the start of the text and, unlike iters, stay
   * meaningful across the insertion. */
  cursor_offset = gtk_text_iter_get_offset (&cursor) + 1 +
                  (int) g_utf8_strlen (indentation, -1) + (int) g_utf8_strlen (unit, -1);

  gtk_text_buffer_begin_user_action (self->buffer);
  gtk_text_buffer_insert_interactive (self->buffer, &cursor, text, -1, editable);
  gtk_text_buffer_get_iter_at_offset (self->buffer, &cursor, cursor_offset);
  gtk_text_buffer_place_cursor (self->buffer, &cursor);
  gtk_text_buffer_end_user_action (self->buffer);

  gtk_text_view_scroll_mark_onscreen (GTK_TEXT_VIEW (self->view),
                                      gtk_text_buffer_get_insert (self->buffer));
  return TRUE;
}

/* ---- Keys --------------------------------------------------------------- */

/* TRUE while a popup of the view is open: the completion list, or a hover
 * tooltip. GtkSourceView shows them as GtkPopovers that are children of
 * the view, and doesn't otherwise tell whether they are open. */
static gboolean
has_open_popup (PalangBracketPairs *self)
{
  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self->view));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    if (GTK_IS_POPOVER (child) && gtk_widget_get_visible (child))
      return TRUE;

  return FALSE;
}

/* A key controller in the capture phase on the view's parent: it sees the
 * view's keys before the view does. It has to come before the view
 * because a GtkTextView types a character with its own key controller,
 * through its input method, before key controllers we could add to the
 * view itself get the key.
 *
 * Returning GDK_EVENT_STOP means the key was handled here and goes no
 * further; GDK_EVENT_PROPAGATE lets it go on to the view as usual. */
static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                PalangBracketPairs    *self)
{
  gunichar ch;

  if (!gtk_widget_has_focus (GTK_WIDGET (self->view)))
    return GDK_EVENT_PROPAGATE;

  /* Ctrl+[ and the like are shortcuts, not typing. (Shift is allowed:
   * it is how "(" and "{" are typed.) */
  if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK))
    return GDK_EVENT_PROPAGATE;

  /* Plain Enter. While the completion list is open, Enter belongs to it
   * (it takes the selected entry): this controller runs before the
   * completion's, so it has to let Enter through. */
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_ISO_Enter)
    {
      if ((state & GDK_SHIFT_MASK) || has_open_popup (self))
        return GDK_EVENT_PROPAGATE;
      return palang_bracket_pairs_newline (self) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
    }

  /* The rest is self-closing brackets, which can be turned off. */
  if (!self->enabled)
    return GDK_EVENT_PROPAGATE;

  if (keyval == GDK_KEY_BackSpace)
    return palang_bracket_pairs_backspace (self) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;

  /* The character the key types, or 0 for keys like F1 or the arrows. */
  ch = gdk_keyval_to_unicode (keyval);
  if (ch != 0 && palang_bracket_pairs_type_char (self, ch))
    return GDK_EVENT_STOP;

  return GDK_EVENT_PROPAGATE;
}

/* ---- GObject boilerplate ------------------------------------------------ */

gboolean
palang_bracket_pairs_get_enabled (PalangBracketPairs *self)
{
  g_return_val_if_fail (PALANG_IS_BRACKET_PAIRS (self), FALSE);

  return self->enabled;
}

void
palang_bracket_pairs_set_enabled (PalangBracketPairs *self,
                                  gboolean            enabled)
{
  g_return_if_fail (PALANG_IS_BRACKET_PAIRS (self));

  enabled = !!enabled;
  if (self->enabled == enabled)
    return;

  self->enabled = enabled;
  if (!enabled)
    g_ptr_array_set_size (self->pending, 0);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

static void
palang_bracket_pairs_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  PalangBracketPairs *self = PALANG_BRACKET_PAIRS (object);

  switch (prop_id)
    {
    case PROP_ENABLED:
      g_value_set_boolean (value, self->enabled);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_bracket_pairs_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  PalangBracketPairs *self = PALANG_BRACKET_PAIRS (object);

  switch (prop_id)
    {
    case PROP_ENABLED:
      palang_bracket_pairs_set_enabled (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_bracket_pairs_dispose (GObject *object)
{
  PalangBracketPairs *self = PALANG_BRACKET_PAIRS (object);

  if (self->buffer != NULL)
    g_signal_handlers_disconnect_by_data (self->buffer, self);

  /* Removes our marks from the buffer (pending_pair_free). */
  if (self->pending != NULL)
    g_ptr_array_set_size (self->pending, 0);

  g_clear_object (&self->buffer);
  g_clear_object (&self->view);

  G_OBJECT_CLASS (palang_bracket_pairs_parent_class)->dispose (object);
}

static void
palang_bracket_pairs_finalize (GObject *object)
{
  PalangBracketPairs *self = PALANG_BRACKET_PAIRS (object);

  g_ptr_array_unref (self->pending);

  G_OBJECT_CLASS (palang_bracket_pairs_parent_class)->finalize (object);
}

static void
palang_bracket_pairs_class_init (PalangBracketPairsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = palang_bracket_pairs_get_property;
  object_class->set_property = palang_bracket_pairs_set_property;
  object_class->dispose = palang_bracket_pairs_dispose;
  object_class->finalize = palang_bracket_pairs_finalize;

  properties[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", NULL, NULL,
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
palang_bracket_pairs_init (PalangBracketPairs *self)
{
  self->enabled = TRUE;
  self->pending = g_ptr_array_new_with_free_func (pending_pair_free);
}

PalangBracketPairs *
palang_bracket_pairs_new (GtkSourceView *view)
{
  PalangBracketPairs *self;
  GtkWidget *parent;
  GtkEventController *keys;

  g_return_val_if_fail (GTK_SOURCE_IS_VIEW (view), NULL);

  parent = gtk_widget_get_parent (GTK_WIDGET (view));
  g_return_val_if_fail (parent != NULL, NULL);

  self = g_object_new (PALANG_TYPE_BRACKET_PAIRS, NULL);
  self->view = g_object_ref (view);
  self->buffer = g_object_ref (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));

  /* "notify::cursor-position" is emitted whenever the cursor moves. */
  g_signal_connect (self->buffer, "notify::cursor-position", G_CALLBACK (on_cursor_moved), self);

  /* In the capture phase an event goes from the window down to the
   * focused widget, so this controller on the parent (the view's
   * GtkScrolledWindow) runs before every controller of the view.
   * g_signal_connect_object (): the connection ends when @self is
   * finalized, so no key reaches an object that is gone. */
  keys = gtk_event_controller_key_new ();
  gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
  g_signal_connect_object (keys, "key-pressed", G_CALLBACK (on_key_pressed), self, 0);
  gtk_widget_add_controller (parent, keys);

  return self;
}
