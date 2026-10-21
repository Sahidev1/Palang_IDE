/* palang-search-panel.c — the Search page of the bottom panel (see the
 * header).
 *
 * Layout is in data/ui/palang-search-panel.ui. The results list shows the
 * PalangSearch's results (in rg's order, which keeps each file's matches
 * together), wrapped in a GtkSingleSelection so a row can be selected.
 *
 * A list view builds row widgets through a "factory": "setup" creates the
 * widgets for a row, "bind" fills them with an item. Rows are recycled as
 * you scroll, so only the visible ones exist. The first match of each file
 * also shows the file's path above it.
 *
 * (GTK's own section headers would do that grouping too, but in GTK 4.14
 * the list keeps the first file's header scrolled just out of view.)
 */

#include "config.h"

#include "palang-search-panel.h"

struct _PalangSearchPanel
{
  AdwBin parent_instance;

  PalangSearch *search;  /* owned */
  GFile        *folder;  /* owned, nullable */

  /* Template children */
  GtkSearchEntry  *entry;
  GtkToggleButton *case_button;
  GtkToggleButton *word_button;
  GtkToggleButton *regex_button;
  GtkLabel        *status_label;
  GtkListView     *list_view;
};

G_DEFINE_FINAL_TYPE (PalangSearchPanel, palang_search_panel, ADW_TYPE_BIN)

enum {
  PROP_0,
  PROP_FOLDER,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum {
  SIGNAL_MATCH_ACTIVATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ---- Searching ---------------------------------------------------------- */

static PalangSearchFlags
get_flags (PalangSearchPanel *self)
{
  PalangSearchFlags flags = 0;

  if (gtk_toggle_button_get_active (self->case_button))
    flags |= PALANG_SEARCH_MATCH_CASE;
  if (gtk_toggle_button_get_active (self->word_button))
    flags |= PALANG_SEARCH_WHOLE_WORD;
  if (gtk_toggle_button_get_active (self->regex_button))
    flags |= PALANG_SEARCH_REGEX;

  return flags;
}

static void
search_now (PalangSearchPanel *self)
{
  const char *text = gtk_editable_get_text (GTK_EDITABLE (self->entry));

  if (self->folder == NULL)
    {
      gtk_label_set_text (self->status_label, "Open a folder to search in it.");
      return;
    }

  gtk_label_set_text (self->status_label, "");
  palang_search_start (self->search, self->folder, text, get_flags (self));
}

/* The text changed (after a pause in typing) or Enter was pressed. */
static void
on_search_changed (GtkSearchEntry    *entry,
                   PalangSearchPanel *self)
{
  search_now (self);
}

static void
on_option_toggled (GtkToggleButton   *button,
                   PalangSearchPanel *self)
{
  search_now (self);
}

/* Escape in the search box. */
static void
on_stop_search (GtkSearchEntry    *entry,
                PalangSearchPanel *self)
{
  palang_search_stop (self->search);
}

/* ---- Status line -------------------------------------------------------- */

/* Number of different files among the results. rg reports all matches of
 * a file together, so counting changes of path is enough. */
static guint
count_files (GListModel *results)
{
  guint n_items = g_list_model_get_n_items (results);
  const char *previous = NULL;
  guint n_files = 0;

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (PalangSearchMatch) match = g_list_model_get_item (results, i);
      const char *path = palang_search_match_get_path (match);

      if (g_strcmp0 (path, previous) != 0)
        n_files++;
      previous = path;  /* still owned by a match in the list */
    }

  return n_files;
}

static void
on_results_changed (GListModel        *results,
                    guint              position,
                    guint              removed,
                    guint              added,
                    PalangSearchPanel *self)
{
  g_autofree char *status = NULL;

  if (!palang_search_is_running (self->search))
    return;

  status = g_strdup_printf ("Searching… %u found", g_list_model_get_n_items (results));
  gtk_label_set_text (self->status_label, status);
}

static void
on_search_finished (PalangSearch      *search,
                    gboolean           truncated,
                    const char        *error_message,
                    PalangSearchPanel *self)
{
  GListModel *results = palang_search_get_results (search);
  guint n_results = g_list_model_get_n_items (results);
  guint n_files;
  g_autofree char *status = NULL;

  if (error_message != NULL)
    {
      gtk_label_set_text (self->status_label, error_message);
      return;
    }

  if (n_results == 0)
    {
      gtk_label_set_text (self->status_label, "No results.");
      return;
    }

  n_files = count_files (results);
  if (truncated)
    status = g_strdup_printf ("The first %u results, in %u %s. Search for something more specific to see all.",
                              n_results, n_files, n_files == 1 ? "file" : "files");
  else
    status = g_strdup_printf ("%u %s in %u %s.",
                              n_results, n_results == 1 ? "result" : "results",
                              n_files, n_files == 1 ? "file" : "files");
  gtk_label_set_text (self->status_label, status);
}

/* ---- Results list ------------------------------------------------------- */

/* A row: the file's path (shown only for the first match of a file), then
 * the line number and the line with the matches in bold. */
static void
on_setup_row (GtkSignalListItemFactory *factory,
              GtkListItem              *item,
              PalangSearchPanel        *self)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *path = gtk_label_new (NULL);
  GtkWidget *line = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *number = gtk_label_new (NULL);
  GtkWidget *text = gtk_label_new (NULL);

  gtk_label_set_xalign (GTK_LABEL (path), 0);
  gtk_label_set_ellipsize (GTK_LABEL (path), PANGO_ELLIPSIZE_START);
  gtk_widget_set_margin_start (path, 6);
  gtk_widget_set_margin_top (path, 6);
  gtk_widget_add_css_class (path, "heading");

  gtk_widget_set_margin_start (line, 12);
  gtk_widget_set_margin_end (line, 6);

  gtk_label_set_xalign (GTK_LABEL (number), 1);
  gtk_label_set_width_chars (GTK_LABEL (number), 5);
  gtk_widget_add_css_class (number, "dim-label");
  gtk_widget_add_css_class (number, "numeric");

  gtk_label_set_xalign (GTK_LABEL (text), 0);
  gtk_label_set_ellipsize (GTK_LABEL (text), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand (text, TRUE);
  gtk_widget_add_css_class (text, "monospace");

  gtk_box_append (GTK_BOX (line), number);
  gtk_box_append (GTK_BOX (line), text);
  gtk_box_append (GTK_BOX (row), path);
  gtk_box_append (GTK_BOX (row), line);
  gtk_list_item_set_child (item, row);
}

/* TRUE if the match at @position is the first one of its file. */
static gboolean
starts_a_file (GListModel *results,
               guint       position)
{
  g_autoptr (PalangSearchMatch) match = NULL;
  g_autoptr (PalangSearchMatch) previous = NULL;

  if (position == 0)
    return TRUE;

  match = g_list_model_get_item (results, position);
  previous = g_list_model_get_item (results, position - 1);
  return g_strcmp0 (palang_search_match_get_path (match),
                    palang_search_match_get_path (previous)) != 0;
}

static void
on_bind_row (GtkSignalListItemFactory *factory,
             GtkListItem              *item,
             PalangSearchPanel        *self)
{
  PalangSearchMatch *match = gtk_list_item_get_item (item);
  GtkWidget *path = gtk_widget_get_first_child (gtk_list_item_get_child (item));
  GtkWidget *number = gtk_widget_get_first_child (gtk_widget_get_next_sibling (path));
  GtkWidget *text = gtk_widget_get_next_sibling (number);
  g_autofree char *line = g_strdup_printf ("%u", palang_search_match_get_line (match));
  g_autofree char *markup = palang_search_match_dup_markup (match);

  gtk_label_set_text (GTK_LABEL (path), palang_search_match_get_path (match));
  gtk_widget_set_visible (path, starts_a_file (palang_search_get_results (self->search),
                                               gtk_list_item_get_position (item)));
  gtk_label_set_text (GTK_LABEL (number), line);
  gtk_label_set_markup (GTK_LABEL (text), markup);
}

/* A row was clicked, or Enter pressed on it. */
static void
on_activate (GtkListView       *list_view,
             guint              position,
             PalangSearchPanel *self)
{
  g_autoptr (PalangSearchMatch) match = g_list_model_get_item (palang_search_get_results (self->search),
                                                               position);

  if (match != NULL)
    g_signal_emit (self, signals[SIGNAL_MATCH_ACTIVATED], 0, match);
}

static void
set_up_list (PalangSearchPanel *self)
{
  GtkListItemFactory *rows = gtk_signal_list_item_factory_new ();
  GtkSingleSelection *selection;

  /* gtk_single_selection_new () takes ownership of the model it's given,
   * hence the extra reference. */
  selection = gtk_single_selection_new (g_object_ref (palang_search_get_results (self->search)));
  gtk_single_selection_set_autoselect (selection, FALSE);
  gtk_single_selection_set_can_unselect (selection, TRUE);

  g_signal_connect (rows, "setup", G_CALLBACK (on_setup_row), self);
  g_signal_connect (rows, "bind", G_CALLBACK (on_bind_row), self);

  gtk_list_view_set_model (self->list_view, GTK_SELECTION_MODEL (selection));
  gtk_list_view_set_factory (self->list_view, rows);

  g_object_unref (selection);
  g_object_unref (rows);
}

/* ---- Public API --------------------------------------------------------- */

void
palang_search_panel_set_folder (PalangSearchPanel *self,
                                GFile             *folder)
{
  g_return_if_fail (PALANG_IS_SEARCH_PANEL (self));
  g_return_if_fail (folder == NULL || G_IS_FILE (folder));

  if (!g_set_object (&self->folder, folder))
    return;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLDER]);

  /* Show results for the new folder (or clear them). */
  search_now (self);
}

GFile *
palang_search_panel_get_folder (PalangSearchPanel *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_PANEL (self), NULL);

  return self->folder;
}

void
palang_search_panel_focus (PalangSearchPanel *self,
                           const char        *text)
{
  g_return_if_fail (PALANG_IS_SEARCH_PANEL (self));

  if (text != NULL)
    {
      /* Setting the text emits "search-changed" only after the delay;
       * search right away instead. */
      gtk_editable_set_text (GTK_EDITABLE (self->entry), text);
      search_now (self);
    }

  gtk_widget_grab_focus (GTK_WIDGET (self->entry));
  gtk_editable_select_region (GTK_EDITABLE (self->entry), 0, -1);
}

PalangSearch *
palang_search_panel_get_search (PalangSearchPanel *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH_PANEL (self), NULL);

  return self->search;
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_search_panel_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  PalangSearchPanel *self = PALANG_SEARCH_PANEL (object);

  switch (prop_id)
    {
    case PROP_FOLDER:
      g_value_set_object (value, self->folder);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_search_panel_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PalangSearchPanel *self = PALANG_SEARCH_PANEL (object);

  switch (prop_id)
    {
    case PROP_FOLDER:
      palang_search_panel_set_folder (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_search_panel_dispose (GObject *object)
{
  PalangSearchPanel *self = PALANG_SEARCH_PANEL (object);

  /* A running search holds a reference to the engine; stop it so the
   * engine can be freed with us. */
  if (self->search != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->search, self);
      g_signal_handlers_disconnect_by_data (palang_search_get_results (self->search), self);
      palang_search_stop (self->search);
    }

  if (self->list_view != NULL)
    gtk_list_view_set_model (self->list_view, NULL);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_SEARCH_PANEL);

  g_clear_object (&self->search);
  g_clear_object (&self->folder);

  G_OBJECT_CLASS (palang_search_panel_parent_class)->dispose (object);
}

static void
palang_search_panel_class_init (PalangSearchPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = palang_search_panel_get_property;
  object_class->set_property = palang_search_panel_set_property;
  object_class->dispose = palang_search_panel_dispose;

  properties[PROP_FOLDER] =
    g_param_spec_object ("folder", NULL, NULL,
                         G_TYPE_FILE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[SIGNAL_MATCH_ACTIVATED] =
    g_signal_new ("match-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, PALANG_TYPE_SEARCH_MATCH);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-search-panel.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangSearchPanel, entry);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchPanel, case_button);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchPanel, word_button);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchPanel, regex_button);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchPanel, status_label);
  gtk_widget_class_bind_template_child (widget_class, PalangSearchPanel, list_view);
  gtk_widget_class_bind_template_callback (widget_class, on_search_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_option_toggled);
  gtk_widget_class_bind_template_callback (widget_class, on_stop_search);
  gtk_widget_class_bind_template_callback (widget_class, on_activate);
}

static void
palang_search_panel_init (PalangSearchPanel *self)
{
  self->search = palang_search_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect (self->search, "finished", G_CALLBACK (on_search_finished), self);
  g_signal_connect (palang_search_get_results (self->search), "items-changed",
                    G_CALLBACK (on_results_changed), self);
  set_up_list (self);

  gtk_label_set_text (self->status_label, "Open a folder to search in it.");
}

PalangSearchPanel *
palang_search_panel_new (void)
{
  return g_object_new (PALANG_TYPE_SEARCH_PANEL, NULL);
}
