/* palang-palette.c — type a few letters, pick from a list (see the header).
 *
 * Layout is in data/ui/palang-palette.ui. Two lists:
 *
 *   items    every choice, as given by palang_palette_set_items ()
 *   matches  the ones matching the typed text, best first; this is what
 *            the list view shows (through a GtkSingleSelection, which
 *            remembers the highlighted row)
 *
 * Each change of the text recomputes `matches` from `items` (refilter).
 */

#include "config.h"

#include <string.h>

#include "palang-fuzzy.h"
#include "palang-palette.h"

struct _PalangPalette
{
  AdwDialog parent_instance;

  GListModel         *items;      /* owned, nullable */
  GListStore         *matches;    /* owned: PalangPaletteItem */
  GtkSingleSelection *selection;  /* owned: wraps matches */

  /* Template children */
  GtkSearchEntry *entry;
  GtkLabel       *status_label;
  GtkListView    *list_view;
};

G_DEFINE_FINAL_TYPE (PalangPalette, palang_palette, ADW_TYPE_DIALOG)

enum {
  SIGNAL_ITEM_ACTIVATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ---- Filtering ---------------------------------------------------------- */

/* One matching item while sorting. */
typedef struct {
  PalangPaletteItem *item;    /* not owned */
  int                score;
  guint              length;  /* of the item's key, in bytes */
  guint              index;   /* position in self->items */
} Candidate;

/* Best score first; equal scores: shorter first; then the original order.
 * (A GCompareFunc returns <0, 0 or >0 like strcmp.) */
static int
compare_candidates (gconstpointer a,
                    gconstpointer b)
{
  const Candidate *x = a;
  const Candidate *y = b;

  if (x->score != y->score)
    return y->score - x->score;
  if (x->length != y->length)
    return (int) x->length - (int) y->length;
  return (int) x->index - (int) y->index;
}

static void
refilter (PalangPalette *self)
{
  const char *text = gtk_editable_get_text (GTK_EDITABLE (self->entry));
  g_autoptr (GArray) candidates = g_array_new (FALSE, FALSE, sizeof (Candidate));
  g_autoptr (GPtrArray) sorted = g_ptr_array_new ();
  guint n_items = self->items != NULL ? g_list_model_get_n_items (self->items) : 0;

  for (guint i = 0; i < n_items; i++)
    {
      /* g_list_model_get_item () returns a new reference; the list keeps
       * its own, so we can drop ours right away and keep the pointer. */
      g_autoptr (PalangPaletteItem) item = g_list_model_get_item (self->items, i);
      const char *key = palang_palette_item_get_key (item);
      Candidate candidate = { item, 0, strlen (key), i };

      if (palang_fuzzy_match (text, key, &candidate.score))
        g_array_append_val (candidates, candidate);
    }

  /* With nothing typed, keep the given order. */
  if (text[0] != '\0')
    g_array_sort (candidates, compare_candidates);

  for (guint i = 0; i < candidates->len; i++)
    g_ptr_array_add (sorted, g_array_index (candidates, Candidate, i).item);

  /* Replace the whole list in one go. */
  g_list_store_splice (self->matches, 0, g_list_model_get_n_items (G_LIST_MODEL (self->matches)),
                       sorted->pdata, sorted->len);

  /* Highlight the best match, so Enter picks it. */
  if (sorted->len > 0)
    {
      gtk_single_selection_set_selected (self->selection, 0);
      gtk_list_view_scroll_to (self->list_view, 0, GTK_LIST_SCROLL_NONE, NULL);
    }
}

static void
on_search_changed (GtkSearchEntry *entry,
                   PalangPalette  *self)
{
  refilter (self);
}

/* ---- Picking ------------------------------------------------------------ */

static void
activate_position (PalangPalette *self,
                   guint          position)
{
  g_autoptr (PalangPaletteItem) item = g_list_model_get_item (G_LIST_MODEL (self->matches), position);

  if (item == NULL)
    return;

  /* Close first, so what the item does (often opening another dialog)
   * happens with the palette out of the way. Closing can drop the last
   * reference to the palette, so hold one until we're done. */
  g_object_ref (self);
  adw_dialog_close (ADW_DIALOG (self));
  g_signal_emit (self, signals[SIGNAL_ITEM_ACTIVATED], 0, item);
  g_object_unref (self);
}

/* Enter in the search box: pick the highlighted row. */
static void
on_entry_activate (GtkSearchEntry *entry,
                   PalangPalette  *self)
{
  guint selected = gtk_single_selection_get_selected (self->selection);

  if (selected != GTK_INVALID_LIST_POSITION)
    activate_position (self, selected);
}

/* A click on a row. */
static void
on_row_activate (GtkListView   *list_view,
                 guint          position,
                 PalangPalette *self)
{
  activate_position (self, position);
}

/* Escape. The search box takes the key for itself, so the dialog wouldn't
 * see it; close from here. */
static void
on_stop_search (GtkSearchEntry *entry,
                PalangPalette  *self)
{
  adw_dialog_close (ADW_DIALOG (self));
}

/* Up and Down move the highlight while the focus stays in the search box.
 * The controller runs in the CAPTURE phase, before the box sees the keys. */
static gboolean
on_entry_key_pressed (GtkEventControllerKey *controller,
                      guint                  keyval,
                      guint                  keycode,
                      GdkModifierType        state,
                      PalangPalette         *self)
{
  guint n_matches = g_list_model_get_n_items (G_LIST_MODEL (self->matches));
  guint selected = gtk_single_selection_get_selected (self->selection);
  guint target;

  if (keyval != GDK_KEY_Up && keyval != GDK_KEY_Down)
    return GDK_EVENT_PROPAGATE;

  if (n_matches == 0)
    return GDK_EVENT_STOP;

  if (selected == GTK_INVALID_LIST_POSITION)
    target = 0;
  else if (keyval == GDK_KEY_Down)
    target = MIN (selected + 1, n_matches - 1);
  else
    target = selected > 0 ? selected - 1 : 0;

  gtk_single_selection_set_selected (self->selection, target);
  gtk_list_view_scroll_to (self->list_view, target, GTK_LIST_SCROLL_NONE, NULL);
  return GDK_EVENT_STOP;
}

/* ---- Rows --------------------------------------------------------------- */

/* A row: the title on the left, the subtitle (shortcut, folder) dimmed on
 * the right. */
static void
on_setup_row (GtkSignalListItemFactory *factory,
              GtkListItem              *list_item,
              PalangPalette            *self)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *title = gtk_label_new (NULL);
  GtkWidget *subtitle = gtk_label_new (NULL);

  gtk_label_set_xalign (GTK_LABEL (title), 0);
  gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand (title, TRUE);

  gtk_label_set_xalign (GTK_LABEL (subtitle), 1);
  gtk_label_set_ellipsize (GTK_LABEL (subtitle), PANGO_ELLIPSIZE_START);
  gtk_widget_add_css_class (subtitle, "dim-label");

  gtk_box_append (GTK_BOX (box), title);
  gtk_box_append (GTK_BOX (box), subtitle);
  gtk_list_item_set_child (list_item, box);
}

static void
on_bind_row (GtkSignalListItemFactory *factory,
             GtkListItem              *list_item,
             PalangPalette            *self)
{
  PalangPaletteItem *item = gtk_list_item_get_item (list_item);
  GtkWidget *title = gtk_widget_get_first_child (gtk_list_item_get_child (list_item));
  GtkWidget *subtitle = gtk_widget_get_next_sibling (title);
  const char *subtitle_text = palang_palette_item_get_subtitle (item);

  gtk_label_set_text (GTK_LABEL (title), palang_palette_item_get_title (item));
  gtk_label_set_text (GTK_LABEL (subtitle), subtitle_text != NULL ? subtitle_text : "");
}

/* ---- Public API --------------------------------------------------------- */

void
palang_palette_set_items (PalangPalette *self,
                          GListModel    *items)
{
  g_return_if_fail (PALANG_IS_PALETTE (self));
  g_return_if_fail (items == NULL || G_IS_LIST_MODEL (items));

  g_set_object (&self->items, items);
  refilter (self);
}

void
palang_palette_set_status (PalangPalette *self,
                           const char    *status)
{
  g_return_if_fail (PALANG_IS_PALETTE (self));

  gtk_label_set_text (self->status_label, status != NULL ? status : "");
  gtk_widget_set_visible (GTK_WIDGET (self->status_label), status != NULL);
}

GListModel *
palang_palette_get_matches (PalangPalette *self)
{
  g_return_val_if_fail (PALANG_IS_PALETTE (self), NULL);

  return G_LIST_MODEL (self->matches);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_palette_dispose (GObject *object)
{
  PalangPalette *self = PALANG_PALETTE (object);

  if (self->list_view != NULL)
    gtk_list_view_set_model (self->list_view, NULL);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_PALETTE);

  g_clear_object (&self->selection);
  g_clear_object (&self->matches);
  g_clear_object (&self->items);

  G_OBJECT_CLASS (palang_palette_parent_class)->dispose (object);
}

static void
palang_palette_class_init (PalangPaletteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = palang_palette_dispose;

  signals[SIGNAL_ITEM_ACTIVATED] =
    g_signal_new ("item-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, PALANG_TYPE_PALETTE_ITEM);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-palette.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangPalette, entry);
  gtk_widget_class_bind_template_child (widget_class, PalangPalette, status_label);
  gtk_widget_class_bind_template_child (widget_class, PalangPalette, list_view);
  gtk_widget_class_bind_template_callback (widget_class, on_search_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_entry_activate);
  gtk_widget_class_bind_template_callback (widget_class, on_stop_search);
  gtk_widget_class_bind_template_callback (widget_class, on_row_activate);
}

static void
palang_palette_init (PalangPalette *self)
{
  GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
  GtkEventController *keys = gtk_event_controller_key_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  self->matches = g_list_store_new (PALANG_TYPE_PALETTE_ITEM);
  /* gtk_single_selection_new () takes ownership of the model it's given. */
  self->selection = gtk_single_selection_new (g_object_ref (G_LIST_MODEL (self->matches)));
  gtk_single_selection_set_autoselect (self->selection, FALSE);

  g_signal_connect (factory, "setup", G_CALLBACK (on_setup_row), self);
  g_signal_connect (factory, "bind", G_CALLBACK (on_bind_row), self);
  gtk_list_view_set_factory (self->list_view, factory);
  gtk_list_view_set_model (self->list_view, GTK_SELECTION_MODEL (self->selection));
  g_object_unref (factory);

  gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
  g_signal_connect (keys, "key-pressed", G_CALLBACK (on_entry_key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self->entry), keys);
}

PalangPalette *
palang_palette_new (const char *placeholder)
{
  PalangPalette *self = g_object_new (PALANG_TYPE_PALETTE, NULL);

  gtk_search_entry_set_placeholder_text (self->entry, placeholder);
  return self;
}
