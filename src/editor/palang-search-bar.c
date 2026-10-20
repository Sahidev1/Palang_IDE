/* palang-search-bar.c — Find / Replace for one editor.
 *
 * GtkSourceView does the searching:
 *   GtkSourceSearchSettings  what to look for (text, case, regex, words)
 *   GtkSourceSearchContext   searches one buffer with those settings,
 *                            highlights all matches and counts them
 *
 * The context exists only while the bar is open; dropping it removes the
 * highlighting. Finding the next match is async (it can take a moment in a
 * big file), so a newer search cancels an unfinished older one.
 */

#include "config.h"

#include "palang-search-bar.h"

struct _PalangSearchBar
{
  AdwBin parent_instance;

  GtkSourceView           *view;         /* owned */
  GtkSourceSearchSettings *settings;     /* owned */
  GtkSourceSearchContext  *context;      /* owned while the bar is open */
  GCancellable            *cancellable;  /* owned; the current find, if any */

  /* TRUE while we change the search entry's text ourselves, so its
   * "changed" handler knows the user didn't type. */
  gboolean updating_entry;

  /* Template children. (The GtkRevealer around them is the AdwBin's direct
   * child; see get_revealer() for why it isn't bound.) */
  GtkSearchEntry  *search_entry;
  GtkLabel        *status_label;
  GtkToggleButton *case_button;
  GtkToggleButton *word_button;
  GtkToggleButton *regex_button;
  GtkToggleButton *replace_toggle;
  GtkEntry        *replace_entry;
};

G_DEFINE_FINAL_TYPE (PalangSearchBar, palang_search_bar, ADW_TYPE_BIN)

/* Binding an AdwBin's direct child as a template child gets it freed twice
 * on dispose (by gtk_widget_dispose_template() and by AdwBin), so we reach
 * the revealer through AdwBin instead. */
static GtkRevealer *
get_revealer (PalangSearchBar *self)
{
  return GTK_REVEALER (adw_bin_get_child (ADW_BIN (self)));
}

static GtkTextBuffer *
get_buffer (PalangSearchBar *self)
{
  return gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->view));
}

/* ---- Status ("2 of 5") -------------------------------------------------- */

static void
update_status (PalangSearchBar *self)
{
  const char *search_text = gtk_source_search_settings_get_search_text (self->settings);
  g_autoptr (GError) regex_error = NULL;
  g_autofree char *status = NULL;
  GtkTextIter start, end;
  int count, position;

  gtk_widget_remove_css_class (GTK_WIDGET (self->search_entry), "error");

  if (self->context == NULL || search_text == NULL)
    {
      gtk_label_set_text (self->status_label, "");
      return;
    }

  regex_error = gtk_source_search_context_get_regex_error (self->context);
  if (regex_error != NULL)
    {
      gtk_widget_add_css_class (GTK_WIDGET (self->search_entry), "error");
      gtk_label_set_text (self->status_label, "Invalid pattern");
      return;
    }

  /* -1 while GtkSourceView is still counting (big files). */
  count = gtk_source_search_context_get_occurrences_count (self->context);
  gtk_text_buffer_get_selection_bounds (get_buffer (self), &start, &end);
  position = gtk_source_search_context_get_occurrence_position (self->context, &start, &end);

  if (count < 0)
    status = g_strdup ("…");
  else if (count == 0)
    status = g_strdup ("No results");
  else if (position > 0)
    status = g_strdup_printf ("%d of %d", position, count);
  else
    status = g_strdup_printf ("%d found", count);

  gtk_label_set_text (self->status_label, status);
}

static void
on_context_changed (GtkSourceSearchContext *context,
                    GParamSpec             *pspec,
                    PalangSearchBar        *self)
{
  update_status (self);
}

/* The cursor or selection moved: "2 of 5" may have changed. */
static void
on_mark_set (GtkTextBuffer   *buffer,
             GtkTextIter     *location,
             GtkTextMark     *mark,
             PalangSearchBar *self)
{
  if (mark == gtk_text_buffer_get_insert (buffer) ||
      mark == gtk_text_buffer_get_selection_bound (buffer))
    update_status (self);
}

/* ---- Finding ------------------------------------------------------------ */

static void
select_match (PalangSearchBar   *self,
              const GtkTextIter *start,
              const GtkTextIter *end)
{
  GtkTextBuffer *buffer = get_buffer (self);

  gtk_text_buffer_select_range (buffer, start, end);
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (self->view),
                                gtk_text_buffer_get_insert (buffer),
                                0.25, FALSE, 0, 0);
  update_status (self);
}

static void
on_forward_done (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr (PalangSearchBar) self = user_data;
  GtkTextIter start, end;

  /* FALSE: no match, or this search was cancelled by a newer one. */
  if (gtk_source_search_context_forward_finish (GTK_SOURCE_SEARCH_CONTEXT (source), result,
                                                &start, &end, NULL, NULL))
    select_match (self, &start, &end);
}

static void
on_backward_done (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr (PalangSearchBar) self = user_data;
  GtkTextIter start, end;

  if (gtk_source_search_context_backward_finish (GTK_SOURCE_SEARCH_CONTEXT (source), result,
                                                 &start, &end, NULL, NULL))
    select_match (self, &start, &end);
}

/* Starts searching from @from; the match found gets selected. */
static void
find_from (PalangSearchBar   *self,
           const GtkTextIter *from,
           gboolean           forward)
{
  if (self->context == NULL)
    return;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  /* wrap-around is on in the settings, so both directions loop. */
  if (forward)
    gtk_source_search_context_forward_async (self->context, from, self->cancellable,
                                             on_forward_done, g_object_ref (self));
  else
    gtk_source_search_context_backward_async (self->context, from, self->cancellable,
                                              on_backward_done, g_object_ref (self));
}

void
palang_search_bar_find (PalangSearchBar *self,
                        gboolean         forward)
{
  GtkTextIter start, end;

  g_return_if_fail (PALANG_IS_SEARCH_BAR (self));

  /* Search after the current selection (forward) or before it (backward),
   * so a selected match isn't found again. */
  gtk_text_buffer_get_selection_bounds (get_buffer (self), &start, &end);
  find_from (self, forward ? &end : &start, forward);
}

/* ---- Replacing ---------------------------------------------------------- */

void
palang_search_bar_replace (PalangSearchBar *self)
{
  const char *replacement;
  GtkTextIter start, end;
  g_autoptr (GError) error = NULL;

  g_return_if_fail (PALANG_IS_SEARCH_BAR (self));

  if (self->context == NULL)
    return;

  gtk_text_buffer_get_selection_bounds (get_buffer (self), &start, &end);

  /* Only replace when the selection *is* a match; otherwise just go to the
   * next match, so the user sees what they're about to replace. */
  if (gtk_source_search_context_get_occurrence_position (self->context, &start, &end) <= 0)
    {
      palang_search_bar_find (self, TRUE);
      return;
    }

  replacement = gtk_editable_get_text (GTK_EDITABLE (self->replace_entry));
  if (!gtk_source_search_context_replace (self->context, &start, &end, replacement, -1, &error))
    {
      gtk_label_set_text (self->status_label, error->message);
      return;
    }

  /* start/end now surround the replacement; continue after it. */
  find_from (self, &end, TRUE);
}

guint
palang_search_bar_replace_all (PalangSearchBar *self)
{
  const char *replacement;
  g_autoptr (GError) error = NULL;
  g_autofree char *status = NULL;
  guint count;

  g_return_val_if_fail (PALANG_IS_SEARCH_BAR (self), 0);

  if (self->context == NULL)
    return 0;

  replacement = gtk_editable_get_text (GTK_EDITABLE (self->replace_entry));

  /* One undo step undoes all the replacements. */
  count = gtk_source_search_context_replace_all (self->context, replacement, -1, &error);
  if (error != NULL)
    {
      gtk_label_set_text (self->status_label, error->message);
      return 0;
    }

  status = g_strdup_printf ("Replaced %u", count);
  gtk_label_set_text (self->status_label, status);
  return count;
}

/* ---- Opening and closing ------------------------------------------------ */

void
palang_search_bar_open (PalangSearchBar *self,
                        gboolean         replace)
{
  GtkTextBuffer *buffer;
  GtkTextIter start, end;

  g_return_if_fail (PALANG_IS_SEARCH_BAR (self));

  buffer = get_buffer (self);

  /* A selection on one line becomes the search text. */
  if (gtk_text_buffer_get_selection_bounds (buffer, &start, &end) &&
      gtk_text_iter_get_line (&start) == gtk_text_iter_get_line (&end))
    {
      g_autofree char *selected = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
      gtk_source_search_settings_set_search_text (self->settings, selected);
    }

  if (self->context == NULL)
    {
      self->context = gtk_source_search_context_new (GTK_SOURCE_BUFFER (buffer), self->settings);
      g_signal_connect_object (self->context, "notify::occurrences-count",
                               G_CALLBACK (on_context_changed), self, 0);
      g_signal_connect_object (self->context, "notify::regex-error",
                               G_CALLBACK (on_context_changed), self, 0);
    }

  gtk_toggle_button_set_active (self->replace_toggle, replace);
  gtk_revealer_set_reveal_child (get_revealer (self), TRUE);

  gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
  gtk_editable_select_region (GTK_EDITABLE (self->search_entry), 0, -1);

  /* Select the first match at or after the cursor. */
  gtk_text_buffer_get_selection_bounds (buffer, &start, &end);
  find_from (self, &start, TRUE);
  update_status (self);
}

void
palang_search_bar_close (PalangSearchBar *self)
{
  g_return_if_fail (PALANG_IS_SEARCH_BAR (self));

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->context);  /* also removes the highlighting */

  gtk_revealer_set_reveal_child (get_revealer (self), FALSE);
  gtk_widget_grab_focus (GTK_WIDGET (self->view));
  update_status (self);
}

gboolean
palang_search_bar_is_open (PalangSearchBar *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_BAR (self), FALSE);

  return gtk_revealer_get_reveal_child (get_revealer (self));
}

/* ---- Accessors ---------------------------------------------------------- */

GtkSourceSearchSettings *
palang_search_bar_get_settings (PalangSearchBar *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_BAR (self), NULL);

  return self->settings;
}

void
palang_search_bar_set_replace_text (PalangSearchBar *self,
                                    const char      *text)
{
  g_return_if_fail (PALANG_IS_SEARCH_BAR (self));

  gtk_editable_set_text (GTK_EDITABLE (self->replace_entry), text);
}

const char *
palang_search_bar_get_status (PalangSearchBar *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_BAR (self), NULL);

  return gtk_label_get_text (self->status_label);
}

/* ---- Keeping the search box and the settings in sync -------------------- */

/* The user typed in the search box: search as they type. */
static void
on_search_entry_changed (GtkSearchEntry  *entry,
                         PalangSearchBar *self)
{
  const char *text = gtk_editable_get_text (GTK_EDITABLE (entry));
  GtkTextIter start, end;

  if (self->updating_entry)
    return;

  /* GtkSourceView uses NULL for "no search", not "". */
  gtk_source_search_settings_set_search_text (self->settings, text[0] != '\0' ? text : NULL);

  /* From the start of the selection, so the current match stays selected
   * while the user keeps typing. */
  gtk_text_buffer_get_selection_bounds (get_buffer (self), &start, &end);
  find_from (self, &start, TRUE);
}

/* The search text was changed in code (e.g. from the selection): show it
 * in the search box. */
static void
on_search_text_changed (GtkSourceSearchSettings *settings,
                        GParamSpec              *pspec,
                        PalangSearchBar         *self)
{
  const char *text = gtk_source_search_settings_get_search_text (settings);

  if (g_strcmp0 (text != NULL ? text : "",
                 gtk_editable_get_text (GTK_EDITABLE (self->search_entry))) == 0)
    return;

  self->updating_entry = TRUE;
  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), text != NULL ? text : "");
  self->updating_entry = FALSE;
}

/* ---- Button and key handlers (connected in the .ui file) ---------------- */

static void
on_search_entry_activate (GtkSearchEntry  *entry,
                          PalangSearchBar *self)
{
  palang_search_bar_find (self, TRUE);
}

/* Esc in the search box. */
static void
on_stop_search (GtkSearchEntry  *entry,
                PalangSearchBar *self)
{
  palang_search_bar_close (self);
}

static void
on_previous_clicked (GtkButton       *button,
                     PalangSearchBar *self)
{
  palang_search_bar_find (self, FALSE);
}

static void
on_next_clicked (GtkButton       *button,
                 PalangSearchBar *self)
{
  palang_search_bar_find (self, TRUE);
}

static void
on_close_clicked (GtkButton       *button,
                  PalangSearchBar *self)
{
  palang_search_bar_close (self);
}

/* Shared by the Replace button and Enter in the replace box. */
static void
on_replace_clicked (GtkWidget       *widget,
                    PalangSearchBar *self)
{
  palang_search_bar_replace (self);
}

static void
on_replace_all_clicked (GtkButton       *button,
                        PalangSearchBar *self)
{
  palang_search_bar_replace_all (self);
}

/* Key bindings (see class_init). Returning TRUE means "handled". */
static gboolean
close_binding (GtkWidget *widget,
               GVariant  *args,
               gpointer   user_data)
{
  palang_search_bar_close (PALANG_SEARCH_BAR (widget));
  return TRUE;
}

static gboolean
find_previous_binding (GtkWidget *widget,
                       GVariant  *args,
                       gpointer   user_data)
{
  palang_search_bar_find (PALANG_SEARCH_BAR (widget), FALSE);
  return TRUE;
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_search_bar_dispose (GObject *object)
{
  PalangSearchBar *self = PALANG_SEARCH_BAR (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->context);

  if (self->view != NULL)
    g_signal_handlers_disconnect_by_data (get_buffer (self), self);
  g_clear_object (&self->view);
  g_clear_object (&self->settings);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_SEARCH_BAR);

  G_OBJECT_CLASS (palang_search_bar_parent_class)->dispose (object);
}

static void
palang_search_bar_class_init (PalangSearchBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = palang_search_bar_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-search-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangSearchBar, search_entry);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchBar, status_label);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchBar, case_button);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchBar, word_button);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchBar, regex_button);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchBar, replace_toggle);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchBar, replace_entry);

  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_activate);
  gtk_widget_class_bind_template_callback (widget_class, on_stop_search);
  gtk_widget_class_bind_template_callback (widget_class, on_previous_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_next_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_close_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_replace_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_replace_all_clicked);

  /* Esc anywhere in the bar closes it (the search box handles its own Esc
   * via "stop-search"); Shift+Enter goes to the previous match. */
  gtk_widget_class_add_binding (widget_class, GDK_KEY_Escape, 0, close_binding, NULL);
  gtk_widget_class_add_binding (widget_class, GDK_KEY_Return, GDK_SHIFT_MASK,
                                find_previous_binding, NULL);
}

static void
palang_search_bar_init (PalangSearchBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = gtk_source_search_settings_new ();
  gtk_source_search_settings_set_wrap_around (self->settings, TRUE);

  /* The option buttons and the settings always agree, whichever changes. */
  g_object_bind_property (self->settings, "case-sensitive", self->case_button, "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_bind_property (self->settings, "at-word-boundaries", self->word_button, "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_bind_property (self->settings, "regex-enabled", self->regex_button, "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  g_signal_connect (self->settings, "notify::search-text",
                    G_CALLBACK (on_search_text_changed), self);
}

PalangSearchBar *
palang_search_bar_new (GtkSourceView *view)
{
  PalangSearchBar *self;

  g_return_val_if_fail (GTK_SOURCE_IS_VIEW (view), NULL);

  self = g_object_new (PALANG_TYPE_SEARCH_BAR, NULL);
  self->view = g_object_ref (view);

  g_signal_connect (get_buffer (self), "mark-set", G_CALLBACK (on_mark_set), self);

  return self;
}
