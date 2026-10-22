/* palang-lsp-problems-panel.c — the "Problems" page (see the header).
 *
 * Layout is in data/ui/palang-lsp-problems-panel.ui: a GtkStack with an
 * "empty" note and the "list". The list view shows the problems model
 * through a GtkSingleSelection (so the keyboard has a highlighted row);
 * each row is built by a GtkSignalListItemFactory, like the palette's.
 */

#include "config.h"

#include "palang-lsp-problems-panel.h"

struct _PalangLspProblemsPanel
{
  AdwBin parent_instance;

  GListModel         *problems;   /* owned */
  GtkSingleSelection *selection;  /* owned: wraps problems */

  /* Template children */
  GtkListView *list_view;
};

G_DEFINE_FINAL_TYPE (PalangLspProblemsPanel, palang_lsp_problems_panel, ADW_TYPE_BIN)

enum {
  SIGNAL_PROBLEM_ACTIVATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ---- Empty or not ------------------------------------------------------- */

static void
update_page (PalangLspProblemsPanel *self)
{
  GtkStack *stack = GTK_STACK (adw_bin_get_child (ADW_BIN (self)));

  gtk_stack_set_visible_child_name (stack, g_list_model_get_n_items (self->problems) > 0
                                           ? "list" : "empty");
}

static void
on_problems_changed (GListModel             *problems,
                     guint                   position,
                     guint                   removed,
                     guint                   added,
                     PalangLspProblemsPanel *self)
{
  update_page (self);
}

/* ---- Activating --------------------------------------------------------- */

static void
activate_position (PalangLspProblemsPanel *self,
                   guint                   position)
{
  g_autoptr (PalangLspProblem) problem = g_list_model_get_item (self->problems, position);

  if (problem != NULL)
    g_signal_emit (self, signals[SIGNAL_PROBLEM_ACTIVATED], 0, problem);
}

/* Enter on a row, or a double click. */
static void
on_row_activate (GtkListView            *list_view,
                 guint                   position,
                 PalangLspProblemsPanel *self)
{
  activate_position (self, position);
}

/* One click on a row (a click gesture per row, as in the palette, so
 * moving the pointer doesn't select rows). */
static void
on_row_clicked (GtkGestureClick *click,
                int              n_press,
                double           x,
                double           y,
                GtkListItem     *list_item)
{
  GtkWidget *row = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (click));
  PalangLspProblemsPanel *self =
    PALANG_LSP_PROBLEMS_PANEL (gtk_widget_get_ancestor (row, PALANG_TYPE_LSP_PROBLEMS_PANEL));

  activate_position (self, gtk_list_item_get_position (list_item));
}

/* ---- Rows --------------------------------------------------------------- */

/* A row: severity icon, the message, and "source · file:line" dimmed on
 * the right. */
static void
on_setup_row (GtkSignalListItemFactory *factory,
              GtkListItem              *list_item,
              PalangLspProblemsPanel   *self)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *icon = gtk_image_new ();
  GtkWidget *message = gtk_label_new (NULL);
  GtkWidget *where = gtk_label_new (NULL);
  GtkGesture *click = gtk_gesture_click_new ();

  gtk_label_set_xalign (GTK_LABEL (message), 0);
  gtk_label_set_ellipsize (GTK_LABEL (message), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode (GTK_LABEL (message), TRUE);
  gtk_widget_set_hexpand (message, TRUE);

  gtk_label_set_xalign (GTK_LABEL (where), 1);
  gtk_widget_add_css_class (where, "dim-label");

  gtk_box_append (GTK_BOX (box), icon);
  gtk_box_append (GTK_BOX (box), message);
  gtk_box_append (GTK_BOX (box), where);

  /* The list item outlives its row widget (and so this gesture). */
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_PRIMARY);
  g_signal_connect (click, "released", G_CALLBACK (on_row_clicked), list_item);
  gtk_widget_add_controller (box, GTK_EVENT_CONTROLLER (click));

  gtk_list_item_set_child (list_item, box);
}

/* The icon and its color for a severity. "error", "warning" and "accent"
 * are libadwaita style classes that color a symbolic icon. */
static void
set_severity_icon (GtkWidget         *icon,
                   PalangLspSeverity  severity)
{
  gtk_widget_remove_css_class (icon, "error");
  gtk_widget_remove_css_class (icon, "warning");
  gtk_widget_remove_css_class (icon, "accent");

  switch (severity)
    {
    case PALANG_LSP_SEVERITY_ERROR:
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), "dialog-error-symbolic");
      gtk_widget_add_css_class (icon, "error");
      break;
    case PALANG_LSP_SEVERITY_WARNING:
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), "dialog-warning-symbolic");
      gtk_widget_add_css_class (icon, "warning");
      break;
    default:
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), "dialog-information-symbolic");
      gtk_widget_add_css_class (icon, "accent");
      break;
    }
}

static void
on_bind_row (GtkSignalListItemFactory *factory,
             GtkListItem              *list_item,
             PalangLspProblemsPanel   *self)
{
  PalangLspProblem *problem = gtk_list_item_get_item (list_item);
  GtkWidget *box = gtk_list_item_get_child (list_item);
  GtkWidget *icon = gtk_widget_get_first_child (box);
  GtkWidget *message = gtk_widget_get_next_sibling (icon);
  GtkWidget *where = gtk_widget_get_next_sibling (message);
  const char *source = palang_lsp_problem_get_source (problem);
  g_autofree char *name = g_file_get_basename (palang_lsp_problem_get_file (problem));
  g_autofree char *path = g_file_get_path (palang_lsp_problem_get_file (problem));
  g_autofree char *location = NULL;
  g_autofree char *tooltip = NULL;

  set_severity_icon (icon, palang_lsp_problem_get_severity (problem));

  /* Single-line mode shows a message's line breaks as spaces; the
   * tooltip has the message as written. */
  gtk_label_set_text (GTK_LABEL (message), palang_lsp_problem_get_message (problem));

  /* Lines count from 1 for people. */
  if (source != NULL)
    location = g_strdup_printf ("%s · %s:%u", source, name, palang_lsp_problem_get_line (problem) + 1);
  else
    location = g_strdup_printf ("%s:%u", name, palang_lsp_problem_get_line (problem) + 1);
  gtk_label_set_text (GTK_LABEL (where), location);

  tooltip = g_strdup_printf ("%s\n%s:%u", palang_lsp_problem_get_message (problem),
                             path != NULL ? path : name, palang_lsp_problem_get_line (problem) + 1);
  gtk_widget_set_tooltip_text (box, tooltip);
}

/* ---- Public API --------------------------------------------------------- */

void
palang_lsp_problems_panel_focus (PalangLspProblemsPanel *self)
{
  g_return_if_fail (PALANG_IS_LSP_PROBLEMS_PANEL (self));

  if (g_list_model_get_n_items (self->problems) == 0)
    return;

  /* GTK_LIST_SCROLL_FOCUS moves the focus to the row as well. */
  gtk_list_view_scroll_to (self->list_view, 0, GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT,
                           NULL);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_lsp_problems_panel_dispose (GObject *object)
{
  PalangLspProblemsPanel *self = PALANG_LSP_PROBLEMS_PANEL (object);

  if (self->list_view != NULL)
    gtk_list_view_set_model (self->list_view, NULL);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_LSP_PROBLEMS_PANEL);

  if (self->problems != NULL)
    g_signal_handlers_disconnect_by_data (self->problems, self);
  g_clear_object (&self->selection);
  g_clear_object (&self->problems);

  G_OBJECT_CLASS (palang_lsp_problems_panel_parent_class)->dispose (object);
}

static void
palang_lsp_problems_panel_class_init (PalangLspProblemsPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = palang_lsp_problems_panel_dispose;

  signals[SIGNAL_PROBLEM_ACTIVATED] =
    g_signal_new ("problem-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, PALANG_TYPE_LSP_PROBLEM);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-lsp-problems-panel.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangLspProblemsPanel, list_view);
  gtk_widget_class_bind_template_callback (widget_class, on_row_activate);
}

static void
palang_lsp_problems_panel_init (PalangLspProblemsPanel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

PalangLspProblemsPanel *
palang_lsp_problems_panel_new (GListModel *problems)
{
  PalangLspProblemsPanel *self;
  GtkListItemFactory *factory;

  g_return_val_if_fail (G_IS_LIST_MODEL (problems), NULL);

  self = g_object_new (PALANG_TYPE_LSP_PROBLEMS_PANEL, NULL);
  self->problems = g_object_ref (problems);

  /* gtk_single_selection_new () takes ownership of the model it's given. */
  self->selection = gtk_single_selection_new (g_object_ref (problems));
  gtk_single_selection_set_autoselect (self->selection, FALSE);
  gtk_single_selection_set_can_unselect (self->selection, TRUE);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (on_setup_row), self);
  g_signal_connect (factory, "bind", G_CALLBACK (on_bind_row), self);
  gtk_list_view_set_factory (self->list_view, factory);
  gtk_list_view_set_model (self->list_view, GTK_SELECTION_MODEL (self->selection));
  g_object_unref (factory);

  g_signal_connect (problems, "items-changed", G_CALLBACK (on_problems_changed), self);
  update_page (self);
  return self;
}
