/* palang-file-tree.c — a GtkListView showing a folder as a tree.
 *
 * How the tree is built (all GTK list models, nested like this):
 *
 *   GtkTreeListModel          flattens the tree into rows; asks us for the
 *    │                        children of a row when it's expanded
 *    └ per folder:
 *       GtkSortListModel      folders first, then by name
 *        └ GtkFilterListModel hides hidden files (unless show-hidden)
 *           └ PalangDirectoryModel  reads the folder in the background and
 *                                   follows changes (see its header for why
 *                                   not GTK's GtkDirectoryList)
 *
 * Every folder level shares the same filter and sorter objects, so toggling
 * show-hidden updates the whole tree at once.
 */

#include "config.h"

#include <string.h>

#include "palang-directory-model.h"
#include "palang-file-tree.h"

/* File details to read for each entry. */
#define FILE_ATTRIBUTES \
  "standard::name,standard::display-name,standard::type," \
  "standard::symbolic-icon,standard::is-hidden"

struct _PalangFileTree
{
  AdwBin parent_instance;

  GFile            *root;         /* owned, nullable */
  gboolean          show_hidden;
  GtkFilter        *filter;       /* owned, shared by all folder levels */
  GtkSorter        *sorter;       /* owned, shared by all folder levels */
  GtkTreeListModel *tree_model;   /* owned, nullable */

  /* Template child. (The GtkStack around it is reached through get_stack():
   * binding the AdwBin's direct child as a template child makes it get
   * freed twice on dispose — once by gtk_widget_dispose_template(), once by
   * AdwBin.) */
  GtkListView *list_view;
};

G_DEFINE_FINAL_TYPE (PalangFileTree, palang_file_tree, ADW_TYPE_BIN)

enum {
  PROP_0,
  PROP_ROOT,
  PROP_SHOW_HIDDEN,
  N_PROPS
};

enum {
  SIGNAL_FILE_ACTIVATED,
  N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

/* PalangDirectoryModel stores each entry's GFile in the "standard::file"
 * attribute of its GFileInfo. */
static GFile *
file_for_info (GFileInfo *info)
{
  return G_FILE (g_file_info_get_attribute_object (info, "standard::file"));
}

static GtkStack *
get_stack (PalangFileTree *self)
{
  return GTK_STACK (adw_bin_get_child (ADW_BIN (self)));
}

static gboolean
is_folder (GFileInfo *info)
{
  return g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY;
}

GFile *
palang_file_tree_row_get_file (GtkTreeListRow *row)
{
  /* get_item returns a new reference; the GFile itself is owned by the
   * GFileInfo, which the row keeps alive. */
  g_autoptr (GFileInfo) info = gtk_tree_list_row_get_item (row);

  return file_for_info (info);
}

/* ---- Filtering and sorting ---------------------------------------------- */

static gboolean
filter_entry (gpointer item,
              gpointer user_data)
{
  PalangFileTree *self = user_data;
  GFileInfo *info = item;

  /* On Linux, "hidden" means the name starts with a dot (or is listed in a
   * .hidden file). */
  return self->show_hidden || !g_file_info_get_is_hidden (info);
}

static int
compare_entries (gconstpointer a,
                 gconstpointer b,
                 gpointer      user_data)
{
  GFileInfo *info_a = G_FILE_INFO ((gpointer) a);
  GFileInfo *info_b = G_FILE_INFO ((gpointer) b);
  g_autofree char *key_a = NULL;
  g_autofree char *key_b = NULL;

  if (is_folder (info_a) != is_folder (info_b))
    return is_folder (info_a) ? -1 : 1;

  /* Collation keys "for filename" sort like a file manager: case-aware and
   * with numbers in natural order (file2 before file10). */
  key_a = g_utf8_collate_key_for_filename (g_file_info_get_display_name (info_a), -1);
  key_b = g_utf8_collate_key_for_filename (g_file_info_get_display_name (info_b), -1);
  return strcmp (key_a, key_b);
}

/* ---- Models ------------------------------------------------------------- */

/* Returns: (transfer full): the sorted, filtered contents of @folder. */
static GListModel *
create_folder_model (PalangFileTree *self,
                     GFile          *folder)
{
  PalangDirectoryModel *directory = palang_directory_model_new (folder, FILE_ATTRIBUTES);
  GtkFilterListModel *filtered;
  GtkSortListModel *sorted;

  /* These constructors take ownership of their arguments ("transfer full"),
   * hence the extra refs on the shared filter and sorter. */
  filtered = gtk_filter_list_model_new (G_LIST_MODEL (directory), g_object_ref (self->filter));
  sorted = gtk_sort_list_model_new (G_LIST_MODEL (filtered), g_object_ref (self->sorter));

  return G_LIST_MODEL (sorted);
}

/* Called by GtkTreeListModel when a row is expanded: returns the row's
 * children, or NULL if it can't have any (a file). */
static GListModel *
create_child_model (gpointer item,
                    gpointer user_data)
{
  GFileInfo *info = item;

  if (!is_folder (info))
    return NULL;

  return create_folder_model (user_data, file_for_info (info));
}

/* ---- Rows --------------------------------------------------------------- */

/* A row's widgets: expander arrow → [icon  name]. Created once per visible
 * row slot and reused as the list scrolls. */
static void
on_setup_row (GtkSignalListItemFactory *factory,
              GtkListItem              *list_item,
              PalangFileTree           *self)
{
  GtkWidget *expander = gtk_tree_expander_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *icon = gtk_image_new ();
  GtkWidget *label = gtk_label_new (NULL);

  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

  gtk_box_append (GTK_BOX (box), icon);
  gtk_box_append (GTK_BOX (box), label);
  gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), box);
  gtk_list_item_set_child (list_item, expander);
}

/* Fills a row's widgets with one entry's icon and name. */
static void
on_bind_row (GtkSignalListItemFactory *factory,
             GtkListItem              *list_item,
             PalangFileTree           *self)
{
  GtkTreeListRow *row = gtk_list_item_get_item (list_item);
  GtkTreeExpander *expander = GTK_TREE_EXPANDER (gtk_list_item_get_child (list_item));
  GtkWidget *box = gtk_tree_expander_get_child (expander);
  GtkWidget *icon = gtk_widget_get_first_child (box);
  GtkWidget *label = gtk_widget_get_next_sibling (icon);
  g_autoptr (GFileInfo) info = gtk_tree_list_row_get_item (row);

  gtk_tree_expander_set_list_row (expander, row);
  gtk_image_set_from_gicon (GTK_IMAGE (icon), g_file_info_get_symbolic_icon (info));
  gtk_label_set_label (GTK_LABEL (label), g_file_info_get_display_name (info));
}

/* A row was clicked (or Enter pressed on it). */
static void
on_activate (GtkListView    *list_view,
             guint           position,
             PalangFileTree *self)
{
  GListModel *rows = G_LIST_MODEL (gtk_list_view_get_model (list_view));
  g_autoptr (GtkTreeListRow) row = g_list_model_get_item (rows, position);
  g_autoptr (GFileInfo) info = gtk_tree_list_row_get_item (row);

  if (is_folder (info))
    gtk_tree_list_row_set_expanded (row, !gtk_tree_list_row_get_expanded (row));
  else
    g_signal_emit (self, signals[SIGNAL_FILE_ACTIVATED], 0, file_for_info (info));
}

/* ---- Public API --------------------------------------------------------- */

void
palang_file_tree_set_root (PalangFileTree *self,
                           GFile          *root)
{
  GtkSingleSelection *selection;

  g_return_if_fail (PALANG_IS_FILE_TREE (self));
  g_return_if_fail (root == NULL || G_IS_FILE (root));

  if (root != NULL && self->root != NULL && g_file_equal (root, self->root))
    return;

  g_set_object (&self->root, root);
  gtk_list_view_set_model (self->list_view, NULL);
  g_clear_object (&self->tree_model);

  if (root != NULL)
    {
      self->tree_model = gtk_tree_list_model_new (create_folder_model (self, root),
                                                  FALSE,  /* passthrough: give us rows */
                                                  FALSE,  /* autoexpand: start collapsed */
                                                  create_child_model, self, NULL);

      /* The list view needs a selection model; nothing starts selected. */
      selection = gtk_single_selection_new (g_object_ref (G_LIST_MODEL (self->tree_model)));
      gtk_single_selection_set_autoselect (selection, FALSE);
      gtk_single_selection_set_can_unselect (selection, TRUE);
      gtk_list_view_set_model (self->list_view, GTK_SELECTION_MODEL (selection));
      g_object_unref (selection);
    }

  gtk_stack_set_visible_child_name (get_stack (self), root != NULL ? "tree" : "empty");
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROOT]);
}

GFile *
palang_file_tree_get_root (PalangFileTree *self)
{
  g_return_val_if_fail (PALANG_IS_FILE_TREE (self), NULL);

  return self->root;
}

void
palang_file_tree_set_show_hidden (PalangFileTree *self,
                                  gboolean        show_hidden)
{
  g_return_if_fail (PALANG_IS_FILE_TREE (self));

  show_hidden = !!show_hidden;
  if (self->show_hidden == show_hidden)
    return;

  self->show_hidden = show_hidden;

  /* Tell every filtered folder level to re-run the filter. */
  gtk_filter_changed (self->filter,
                      show_hidden ? GTK_FILTER_CHANGE_LESS_STRICT : GTK_FILTER_CHANGE_MORE_STRICT);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_HIDDEN]);
}

gboolean
palang_file_tree_get_show_hidden (PalangFileTree *self)
{
  g_return_val_if_fail (PALANG_IS_FILE_TREE (self), FALSE);

  return self->show_hidden;
}

GListModel *
palang_file_tree_get_rows (PalangFileTree *self)
{
  g_return_val_if_fail (PALANG_IS_FILE_TREE (self), NULL);

  return G_LIST_MODEL (self->tree_model);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_file_tree_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  PalangFileTree *self = PALANG_FILE_TREE (object);

  switch (prop_id)
    {
    case PROP_ROOT:
      g_value_set_object (value, self->root);
      break;

    case PROP_SHOW_HIDDEN:
      g_value_set_boolean (value, self->show_hidden);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_file_tree_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  PalangFileTree *self = PALANG_FILE_TREE (object);

  switch (prop_id)
    {
    case PROP_ROOT:
      palang_file_tree_set_root (self, g_value_get_object (value));
      break;

    case PROP_SHOW_HIDDEN:
      palang_file_tree_set_show_hidden (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_file_tree_dispose (GObject *object)
{
  PalangFileTree *self = PALANG_FILE_TREE (object);

  /* Drop the models first: they use the filter and sorter, whose callbacks
   * point back at self. */
  if (self->list_view != NULL)
    gtk_list_view_set_model (self->list_view, NULL);
  g_clear_object (&self->tree_model);
  g_clear_object (&self->filter);
  g_clear_object (&self->sorter);
  g_clear_object (&self->root);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_FILE_TREE);

  G_OBJECT_CLASS (palang_file_tree_parent_class)->dispose (object);
}

static void
palang_file_tree_class_init (PalangFileTreeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = palang_file_tree_get_property;
  object_class->set_property = palang_file_tree_set_property;
  object_class->dispose = palang_file_tree_dispose;

  properties[PROP_ROOT] =
    g_param_spec_object ("root", NULL, NULL,
                         G_TYPE_FILE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  properties[PROP_SHOW_HIDDEN] =
    g_param_spec_boolean ("show-hidden", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  /* "file-activated" (PalangFileTree *tree, GFile *file): a file was
   * clicked. */
  signals[SIGNAL_FILE_ACTIVATED] =
    g_signal_new ("file-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_FILE);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-file-tree.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangFileTree, list_view);
  gtk_widget_class_bind_template_callback (widget_class, on_setup_row);
  gtk_widget_class_bind_template_callback (widget_class, on_bind_row);
  gtk_widget_class_bind_template_callback (widget_class, on_activate);
}

static void
palang_file_tree_init (PalangFileTree *self)
{
  self->filter = GTK_FILTER (gtk_custom_filter_new (filter_entry, self, NULL));
  self->sorter = GTK_SORTER (gtk_custom_sorter_new (compare_entries, NULL, NULL));

  gtk_widget_init_template (GTK_WIDGET (self));
}

PalangFileTree *
palang_file_tree_new (void)
{
  return g_object_new (PALANG_TYPE_FILE_TREE, NULL);
}
