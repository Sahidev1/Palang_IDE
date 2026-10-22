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
  "standard::fast-content-type,standard::is-hidden"

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

/* Content types shown as plain text rather than code. Most code types
 * (text/x-csrc, text/x-python, …) are subtypes of text/plain too. */
static const char *const prose_types[] = {
  "text/plain", "text/markdown", "text/x-readme", "text/x-log", "text/csv",
};

/* The name of an entry's icon: Palang's own (data/icons/). The type comes
 * from "fast-content-type", a guess from the file's name alone, so listing a
 * big folder doesn't read every file. */
static const char *
icon_name_for (GFileInfo *info,
               gboolean   expanded)
{
  const char *type;

  if (is_folder (info))
    return expanded ? "palang-folder-open-symbolic" : "palang-folder-symbolic";

  type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
  if (type == NULL)
    return "palang-file-symbolic";

  if (g_str_has_prefix (type, "image/"))
    return "palang-file-image-symbolic";

  for (gsize i = 0; i < G_N_ELEMENTS (prose_types); i++)
    if (g_content_type_equals (type, prose_types[i]))
      return "palang-file-text-symbolic";

  if (g_content_type_is_a (type, "text/plain"))
    return "palang-file-code-symbolic";

  return "palang-file-symbolic";
}

/* Sets a row's icon from its entry and whether it is expanded. */
static void
update_row_icon (GtkTreeListRow *row,
                 GtkImage       *icon)
{
  g_autoptr (GFileInfo) info = gtk_tree_list_row_get_item (row);

  gtk_image_set_from_icon_name (icon, icon_name_for (info, gtk_tree_list_row_get_expanded (row)));
}

/* A folder opened or closed: its icon follows. */
static void
on_row_expanded (GtkTreeListRow *row,
                 GParamSpec     *pspec,
                 GtkImage       *icon)
{
  update_row_icon (row, icon);
}

static void on_row_pressed (GtkGestureClick *click,
                            int              n_press,
                            double           x,
                            double           y,
                            GtkListItem     *list_item);

/* A row's widgets: expander arrow → [icon  name]. Created once per visible
 * row slot and reused as the list scrolls. The click gesture on [icon name]
 * opens the entry with one click (see on_row_pressed ()). */
static void
on_setup_row (GtkSignalListItemFactory *factory,
              GtkListItem              *list_item,
              PalangFileTree           *self)
{
  GtkWidget *expander = gtk_tree_expander_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *icon = gtk_image_new ();
  GtkWidget *label = gtk_label_new (NULL);
  GtkGesture *click = gtk_gesture_click_new ();

  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

  /* The list item outlives its row widgets (and so this gesture), so it
   * is safe to pass as user data. */
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_PRIMARY);
  g_signal_connect (click, "pressed", G_CALLBACK (on_row_pressed), list_item);
  gtk_widget_add_controller (box, GTK_EVENT_CONTROLLER (click));

  /* Fill the row, so a click anywhere right of the arrow hits the box
   * (data/style.css moves the row's padding into the box, too). */
  gtk_widget_set_hexpand (box, TRUE);
  gtk_widget_add_css_class (box, "palang-file-tree-row");

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
  update_row_icon (row, GTK_IMAGE (icon));
  gtk_label_set_label (GTK_LABEL (label), g_file_info_get_display_name (info));

  /* "notify::<property>" is emitted when a property changes. The row widgets
   * are reused for other rows as the list scrolls, so on_unbind_row ()
   * disconnects this again. */
  g_signal_connect (row, "notify::expanded", G_CALLBACK (on_row_expanded), icon);
}

/* A row's widgets are about to show another entry: stop following this one. */
static void
on_unbind_row (GtkSignalListItemFactory *factory,
               GtkListItem              *list_item,
               PalangFileTree           *self)
{
  GtkTreeListRow *row = gtk_list_item_get_item (list_item);
  GtkTreeExpander *expander = GTK_TREE_EXPANDER (gtk_list_item_get_child (list_item));
  GtkWidget *icon = gtk_widget_get_first_child (gtk_tree_expander_get_child (expander));

  g_signal_handlers_disconnect_by_func (row, on_row_expanded, icon);
}

/* A row was activated: Enter pressed on it, or clicked (on_row_pressed). */
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

/* One click on a row opens its file or folds its folder open or closed.
 *
 * GtkListView's own "single-click-activate" would do this too, but it also
 * selects whichever row the pointer passes over, and that row stays
 * highlighted after the pointer leaves the sidebar. So the list opens
 * entries only on double click or Enter (on_activate ()), and this gesture
 * on each row handles the single click.
 *
 * Claiming the click keeps it from the list's own handling: otherwise a
 * double click would also reach on_activate () and fold a folder straight
 * back. So this selects and focuses the row itself, as the list would. */
static void
on_row_pressed (GtkGestureClick *click,
                int              n_press,
                double           x,
                double           y,
                GtkListItem     *list_item)
{
  GtkWidget *box = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (click));
  PalangFileTree *self = PALANG_FILE_TREE (gtk_widget_get_ancestor (box, PALANG_TYPE_FILE_TREE));
  guint position = gtk_list_item_get_position (list_item);

  gtk_gesture_set_state (GTK_GESTURE (click), GTK_EVENT_SEQUENCE_CLAIMED);

  /* The second press of a double click: the first one did the work. */
  if (n_press != 1)
    return;

  gtk_list_view_scroll_to (self->list_view, position,
                           GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT, NULL);
  on_activate (self->list_view, position, self);
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
  gtk_widget_class_bind_template_callback (widget_class, on_unbind_row);
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
