/* palang-lsp-completion-provider.c — completion from language servers
 * (see the header).
 *
 * How GtkSourceView drives a provider:
 *
 *   is_trigger ()      a character was typed: should it open the popup?
 *   populate_async ()  a new completion starts: return a GListModel of
 *                      proposals (we ask the server)
 *   refilter ()        the typed word changed while the popup is open:
 *                      update that model (we filter and sort it again)
 *   display ()         fill one row's cells for one proposal
 *   activate ()        the user picked a proposal: insert it
 *
 * The model we return is a chain of GTK list models:
 *
 *   GListStore (every item the server sent)
 *     → GtkFilterListModel (only items matching the typed text)
 *       → GtkSortListModel (best match first)
 *
 * Changing what is typed only re-scores the items (rescore ()); the
 * chain updates itself and the popup with it.
 *
 * One provider per view: it also watches that view's keys for the Enter
 * rule (see "Enter after a whole word").
 */

#include "config.h"

#include "palang-lsp-completion-item.h"
#include "palang-lsp-completion-provider.h"

/* The name under which a model's Results are attached to it. */
#define RESULTS_KEY "palang-lsp-results"

struct _PalangLspCompletionProvider
{
  GObject parent_instance;

  /* A weak pointer: GLib sets it to NULL when the manager is finalized. */
  PalangLspManager *manager;

  /* For the Enter rule (see "Enter after a whole word" below). */
  GtkSourceView *view;   /* weak pointer: the view we complete in */
  GListModel    *shown;  /* owned, nullable: the list of the last completion */
  gboolean       moved;  /* the selection was moved in it */
};

static void completion_provider_iface_init (GtkSourceCompletionProviderInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (PalangLspCompletionProvider, palang_lsp_completion_provider,
                               G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (GTK_SOURCE_TYPE_COMPLETION_PROVIDER,
                                                      completion_provider_iface_init))

/* ---- The list of one completion ----------------------------------------- */

/* Everything behind the model one completion shows. Attached to that model
 * (g_object_set_data_full), so refilter () finds it again and it is freed
 * with the model. */
typedef struct {
  GListStore   *items;       /* owned: PalangLspCompletionItem */
  char         *typed;       /* owned: casefolded; what the scores are for */
  gboolean      incomplete;  /* the server finds more as more is typed */
  GCancellable *asking;      /* owned, nullable: a request asking again */
} Results;

static void
results_free (Results *results)
{
  g_cancellable_cancel (results->asking);
  g_clear_object (&results->asking);
  g_clear_object (&results->items);
  g_free (results->typed);
  g_free (results);
}

/* GtkCustomFilter's function: keep the items that matched. */
static gboolean
item_matches (gpointer item,
              gpointer user_data)
{
  return palang_lsp_completion_item_get_score (item) != G_MAXUINT;
}

/* GtkCustomSorter's function. */
static int
compare_items (gconstpointer a,
               gconstpointer b,
               gpointer      user_data)
{
  return palang_lsp_completion_item_compare ((gpointer) a, (gpointer) b);
}

/* Matches every item against results->typed again, then puts the same
 * items back into the store. That "items-changed" makes the filter and
 * sorter look at every item again, and makes the popup draw its rows
 * again: a row that stays in the list would otherwise keep the bold
 * letters of what was typed before. */
static void
rescore (Results *results)
{
  GListModel *items = G_LIST_MODEL (results->items);
  g_autoptr (GPtrArray) all = g_ptr_array_new_with_free_func (g_object_unref);

  for (guint i = 0; i < g_list_model_get_n_items (items); i++)
    {
      PalangLspCompletionItem *item = g_list_model_get_item (items, i);

      palang_lsp_completion_item_match (item, results->typed);
      g_ptr_array_add (all, item);  /* the array takes the reference */
    }

  g_list_store_splice (results->items, 0, all->len, all->pdata, all->len);
}

/* Returns: (transfer full): what was typed of the word being completed,
 * casefolded for the fuzzy matcher. */
static char *
dup_typed (GtkSourceCompletionContext *context)
{
  g_autofree char *word = gtk_source_completion_context_get_word (context);

  return g_utf8_casefold (word, -1);
}

/* Returns: (transfer full): the model to show for @items (the server's
 * answer), filtered and sorted for what is typed in @context. */
static GListModel *
new_results_model (GListModel                 *items,
                   gboolean                    incomplete,
                   GtkSourceCompletionContext *context)
{
  Results *results = g_new0 (Results, 1);
  GtkCustomFilter *filter = gtk_custom_filter_new (item_matches, NULL, NULL);
  GtkCustomSorter *sorter = gtk_custom_sorter_new (compare_items, NULL, NULL);
  GtkFilterListModel *filtered;
  GtkSortListModel *sorted;

  results->items = G_LIST_STORE (g_object_ref (items));
  results->typed = dup_typed (context);
  results->incomplete = incomplete;

  /* The _new () functions take ownership of the model and of the filter
   * or sorter they are given; Results keeps its own reference to the
   * items. */
  filtered = gtk_filter_list_model_new (g_object_ref (items), GTK_FILTER (filter));
  sorted = gtk_sort_list_model_new (G_LIST_MODEL (filtered), GTK_SORTER (sorter));

  rescore (results);
  g_object_set_data_full (G_OBJECT (sorted), RESULTS_KEY, results, (GDestroyNotify) results_free);
  return G_LIST_MODEL (sorted);
}

/* ---- Trigger characters ------------------------------------------------- */

/* Returns: TRUE if @ch is one of the characters after which @client's
 * server wants to complete (its "completionProvider.triggerCharacters",
 * e.g. "." and ">" for clangd). */
static gboolean
is_trigger_character (PalangLspClient *client,
                      gunichar         ch)
{
  GVariant *capabilities = palang_lsp_client_get_capabilities (client);
  g_autoptr (GVariant) options = NULL;
  g_autoptr (GVariant) characters = NULL;

  if (capabilities == NULL)
    return FALSE;

  options = g_variant_lookup_value (capabilities, "completionProvider", G_VARIANT_TYPE_VARDICT);
  if (options != NULL)
    characters = g_variant_lookup_value (options, "triggerCharacters", G_VARIANT_TYPE ("av"));

  for (gsize i = 0; characters != NULL && i < g_variant_n_children (characters); i++)
    {
      g_autoptr (GVariant) boxed = g_variant_get_child_value (characters, i);
      g_autoptr (GVariant) character = g_variant_get_variant (boxed);

      if (g_variant_is_of_type (character, G_VARIANT_TYPE_STRING) &&
          g_utf8_get_char (g_variant_get_string (character, NULL)) == ch)
        return TRUE;
    }

  return FALSE;
}

/* Returns: (transfer none) (nullable): the server of the document @buffer
 * is, if it is one of ours. */
static PalangLspClient *
client_for_buffer (PalangLspCompletionProvider *self,
                   GtkTextBuffer               *buffer)
{
  if (self->manager == NULL || !PALANG_IS_DOCUMENT (buffer))
    return NULL;
  return palang_lsp_manager_get_client (self->manager, PALANG_DOCUMENT (buffer));
}

static gboolean
palang_lsp_completion_provider_is_trigger (GtkSourceCompletionProvider *provider,
                                           const GtkTextIter           *iter,
                                           gunichar                     ch)
{
  PalangLspCompletionProvider *self = PALANG_LSP_COMPLETION_PROVIDER (provider);
  PalangLspClient *client = client_for_buffer (self, gtk_text_iter_get_buffer (iter));

  return client != NULL && is_trigger_character (client, ch);
}

/* ---- Asking the server -------------------------------------------------- */

static void
on_completed (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  g_autoptr (GTask) task = user_data;
  PalangLspCompletionProvider *self = g_task_get_source_object (task);
  GtkSourceCompletionContext *context = g_task_get_task_data (task);
  g_autoptr (GListModel) items = NULL;
  g_autoptr (GListModel) model = NULL;
  g_autoptr (GError) error = NULL;
  gboolean incomplete = FALSE;

  items = palang_lsp_manager_complete_finish (PALANG_LSP_MANAGER (source), result,
                                              &incomplete, &error);
  if (items == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* The context's word is what is typed *now*, which may be more than
   * when we asked. */
  model = new_results_model (items, incomplete, context);
  g_set_object (&self->shown, model);
  g_task_return_pointer (task, g_steal_pointer (&model), g_object_unref);
}

static void
palang_lsp_completion_provider_populate_async (GtkSourceCompletionProvider *provider,
                                               GtkSourceCompletionContext  *context,
                                               GCancellable                *cancellable,
                                               GAsyncReadyCallback          callback,
                                               gpointer                     user_data)
{
  PalangLspCompletionProvider *self = PALANG_LSP_COMPLETION_PROVIDER (provider);
  GtkSourceBuffer *buffer = gtk_source_completion_context_get_buffer (context);
  PalangLspClient *client = client_for_buffer (self, GTK_TEXT_BUFFER (buffer));
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  GtkTextIter begin, end, before;
  gunichar trigger = 0;

  g_task_set_task_data (task, g_object_ref (context), g_object_unref);

  /* A new completion: forget the last one's list (for the Enter rule). */
  g_clear_object (&self->shown);
  self->moved = FALSE;

  if (client == NULL || !gtk_source_completion_context_get_bounds (context, &begin, &end))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "No language server for this file");
      g_object_unref (task);
      return;
    }

  /* Nothing of a word typed yet, right after a trigger character ("s." or
   * "p->"): tell the server which one. */
  before = end;
  if (gtk_text_iter_equal (&begin, &end) && gtk_text_iter_backward_char (&before) &&
      is_trigger_character (client, gtk_text_iter_get_char (&before)))
    trigger = gtk_text_iter_get_char (&before);

  palang_lsp_manager_complete_async (self->manager, PALANG_DOCUMENT (buffer), &end, trigger,
                                     cancellable, on_completed, task);
}

static GListModel *
palang_lsp_completion_provider_populate_finish (GtkSourceCompletionProvider  *provider,
                                                GAsyncResult                 *result,
                                                GError                      **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

/* The answer to asking again (Results.incomplete): replaces the items. */
static void
on_asked_again (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr (GListModel) model = user_data;  /* the model being shown */
  Results *results = g_object_get_data (G_OBJECT (model), RESULTS_KEY);
  g_autoptr (GListModel) items = NULL;
  g_autoptr (GPtrArray) new_items = g_ptr_array_new_with_free_func (g_object_unref);
  gboolean incomplete = FALSE;

  items = palang_lsp_manager_complete_finish (PALANG_LSP_MANAGER (source), result,
                                              &incomplete, NULL);
  if (items == NULL)
    return;  /* cancelled (typed on, asked once more) or failed: keep the old list */

  for (guint i = 0; i < g_list_model_get_n_items (items); i++)
    g_ptr_array_add (new_items, g_list_model_get_item (items, i));

  /* g_list_store_splice () replaces items in one go: here all of them. */
  g_list_store_splice (results->items, 0, g_list_model_get_n_items (G_LIST_MODEL (results->items)),
                       new_items->pdata, new_items->len);
  results->incomplete = incomplete;
  rescore (results);
}

static void
palang_lsp_completion_provider_refilter (GtkSourceCompletionProvider *provider,
                                         GtkSourceCompletionContext  *context,
                                         GListModel                  *model)
{
  PalangLspCompletionProvider *self = PALANG_LSP_COMPLETION_PROVIDER (provider);
  Results *results = g_object_get_data (G_OBJECT (model), RESULTS_KEY);
  GtkSourceBuffer *buffer = gtk_source_completion_context_get_buffer (context);
  GtkTextIter begin, end;

  if (results == NULL)
    return;

  g_free (results->typed);
  results->typed = dup_typed (context);
  rescore (results);

  /* The server sent only part of what matches (clangd stops at 100
   * entries): ask again for the longer word. Until the answer comes, the
   * old list, filtered, stays. A newer question cancels an older one. */
  if (!results->incomplete || self->manager == NULL || !PALANG_IS_DOCUMENT (buffer) ||
      !gtk_source_completion_context_get_bounds (context, &begin, &end))
    return;

  g_cancellable_cancel (results->asking);
  g_clear_object (&results->asking);
  results->asking = g_cancellable_new ();
  palang_lsp_manager_complete_async (self->manager, PALANG_DOCUMENT (buffer), &end, 0,
                                     results->asking, on_asked_again, g_object_ref (model));
}

/* ---- Showing and inserting ---------------------------------------------- */

/* A row of the popup has columns (GtkSourceCompletionColumn); we fill
 * three: the label with the typed letters in bold, the detail (often a
 * type) after it, and the kind ("Function") at the end. */
static void
palang_lsp_completion_provider_display (GtkSourceCompletionProvider *provider,
                                        GtkSourceCompletionContext  *context,
                                        GtkSourceCompletionProposal *proposal,
                                        GtkSourceCompletionCell     *cell)
{
  PalangLspCompletionItem *item = PALANG_LSP_COMPLETION_ITEM (proposal);

  switch (gtk_source_completion_cell_get_column (cell))
    {
    case GTK_SOURCE_COMPLETION_COLUMN_TYPED_TEXT:
      {
        const char *label = palang_lsp_completion_item_get_label (item);
        g_autofree char *typed = dup_typed (context);
        PangoAttrList *bold = gtk_source_completion_fuzzy_highlight (label, typed);

        gtk_source_completion_cell_set_text_with_attributes (cell, label, bold);
        g_clear_pointer (&bold, pango_attr_list_unref);
      }
      break;

    case GTK_SOURCE_COMPLETION_COLUMN_AFTER:
      gtk_source_completion_cell_set_text (cell, palang_lsp_completion_item_get_detail (item));
      break;

    case GTK_SOURCE_COMPLETION_COLUMN_COMMENT:
      gtk_source_completion_cell_set_text (cell, palang_lsp_completion_item_get_kind_name (item));
      break;

    default:
      /* Rows are reused for other proposals: clear what we don't use. */
      gtk_source_completion_cell_set_text (cell, NULL);
      break;
    }
}

/* The user picked @proposal: it replaces the typed word. */
static void
palang_lsp_completion_provider_activate (GtkSourceCompletionProvider *provider,
                                         GtkSourceCompletionContext  *context,
                                         GtkSourceCompletionProposal *proposal)
{
  GtkSourceBuffer *buffer = gtk_source_completion_context_get_buffer (context);
  GtkTextIter begin, end;

  if (gtk_source_completion_context_get_bounds (context, &begin, &end))
    palang_lsp_completion_item_insert (PALANG_LSP_COMPLETION_ITEM (proposal),
                                       GTK_TEXT_BUFFER (buffer), &begin, &end);
}

static char *
palang_lsp_completion_provider_get_title (GtkSourceCompletionProvider *provider)
{
  return g_strdup ("Language Server");
}

/* ---- Enter after a whole word ------------------------------------------- */

/* The popup opens with its first entry selected, so Enter or Tab takes it.
 * But after typing a whole word ("return"), the first entry is often that
 * very word, and Enter should start a new line, not "insert" what is
 * there already. So when the popup is open, the selection wasn't moved,
 * and the text before the cursor already is the first entry's text, Enter
 * closes the popup and goes on to the text view. Tab still takes the
 * entry.
 *
 * GtkSourceView doesn't tell which entry is selected, nor when its popup
 * opens and closes (its "show" and "hide" signals are requests, not
 * emitted when the popup opens by itself). So we remember the list of the
 * last completion (self->shown), and whether Up, Down, Page Up or Page
 * Down was pressed since it started: if not, the first entry is selected.
 * If the popup was closed meanwhile (Escape), hiding it again does
 * nothing and Enter starts a new line, as it should. */

static gboolean
is_word_character (gunichar ch)
{
  return g_unichar_isalnum (ch) || ch == '_';
}

/* Returns: TRUE if the whole word before the cursor already is the text
 * the first entry would insert. */
static gboolean
first_entry_is_typed (PalangLspCompletionProvider *self)
{
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->view));
  g_autoptr (PalangLspCompletionItem) first = NULL;
  g_autofree char *before = NULL;
  const char *new_text;
  GtkTextIter start, cursor, previous;

  if (self->shown == NULL)
    return FALSE;

  first = g_list_model_get_item (self->shown, 0);
  if (first == NULL)
    return FALSE;

  new_text = palang_lsp_completion_item_get_new_text (first);
  gtk_text_buffer_get_iter_at_mark (buffer, &cursor, gtk_text_buffer_get_insert (buffer));
  start = cursor;
  if (!gtk_text_iter_backward_chars (&start, g_utf8_strlen (new_text, -1)))
    return FALSE;

  before = gtk_text_iter_get_text (&start, &cursor);
  if (!g_str_equal (before, new_text))
    return FALSE;

  /* "xreturn" isn't the word "return". */
  previous = start;
  return !gtk_text_iter_backward_char (&previous) ||
         !is_word_character (gtk_text_iter_get_char (&previous));
}

/* A key controller in the capture phase on the view's parent (see
 * palang_lsp_completion_provider_new ()): it sees the view's keys before
 * the view and its completion do. It only watches and maybe closes the
 * popup; the key always goes on (PROPAGATE). */
static gboolean
on_view_key_pressed (GtkEventControllerKey       *controller,
                     guint                        keyval,
                     guint                        keycode,
                     GdkModifierType              state,
                     PalangLspCompletionProvider *self)
{
  if (self->shown == NULL || self->view == NULL)
    return GDK_EVENT_PROPAGATE;

  switch (keyval)
    {
    case GDK_KEY_Up:
    case GDK_KEY_Down:
    case GDK_KEY_Page_Up:
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Up:
    case GDK_KEY_KP_Down:
      self->moved = TRUE;
      break;

    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
      if (!self->moved && first_entry_is_typed (self))
        gtk_source_completion_hide (gtk_source_view_get_completion (self->view));
      break;

    default:
      break;
    }

  return GDK_EVENT_PROPAGATE;
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
completion_provider_iface_init (GtkSourceCompletionProviderInterface *iface)
{
  iface->get_title = palang_lsp_completion_provider_get_title;
  iface->is_trigger = palang_lsp_completion_provider_is_trigger;
  iface->populate_async = palang_lsp_completion_provider_populate_async;
  iface->populate_finish = palang_lsp_completion_provider_populate_finish;
  iface->refilter = palang_lsp_completion_provider_refilter;
  iface->display = palang_lsp_completion_provider_display;
  iface->activate = palang_lsp_completion_provider_activate;
}

static void
palang_lsp_completion_provider_dispose (GObject *object)
{
  PalangLspCompletionProvider *self = PALANG_LSP_COMPLETION_PROVIDER (object);

  g_clear_weak_pointer (&self->manager);
  g_clear_weak_pointer (&self->view);
  g_clear_object (&self->shown);

  G_OBJECT_CLASS (palang_lsp_completion_provider_parent_class)->dispose (object);
}

static void
palang_lsp_completion_provider_class_init (PalangLspCompletionProviderClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = palang_lsp_completion_provider_dispose;
}

static void
palang_lsp_completion_provider_init (PalangLspCompletionProvider *self)
{
}

PalangLspCompletionProvider *
palang_lsp_completion_provider_new (PalangLspManager *manager,
                                    GtkSourceView    *view)
{
  PalangLspCompletionProvider *self;
  GtkEventController *keys;

  g_return_val_if_fail (PALANG_IS_LSP_MANAGER (manager), NULL);
  g_return_val_if_fail (GTK_SOURCE_IS_VIEW (view), NULL);

  self = g_object_new (PALANG_TYPE_LSP_COMPLETION_PROVIDER, NULL);
  g_set_weak_pointer (&self->manager, manager);
  g_set_weak_pointer (&self->view, view);

  /* The Enter rule needs to see Enter, Up and Down before the completion
   * does. GtkSourceCompletion takes them with its own capture-phase
   * controller on the view, which runs before any we add to the view. In
   * the capture phase an event goes from the window down to the focused
   * widget, so a controller on the view's parent (its GtkScrolledWindow)
   * runs first. g_signal_connect_object (): the connection ends when
   * @self is finalized, so no key reaches a provider that is gone. */
  if (gtk_widget_get_parent (GTK_WIDGET (view)) != NULL)
    {
      keys = gtk_event_controller_key_new ();
      gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
      g_signal_connect_object (keys, "key-pressed", G_CALLBACK (on_view_key_pressed), self, 0);
      gtk_widget_add_controller (gtk_widget_get_parent (GTK_WIDGET (view)), keys);
    }

  return self;
}
