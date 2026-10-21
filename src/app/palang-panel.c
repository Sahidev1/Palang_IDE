/* palang-panel.c — the bottom panel: switchable pages under the tabs.
 *
 * Layout is in data/ui/palang-panel.ui: a switcher row and an AdwViewStack
 * that holds the pages. The window decides which pages exist (it creates
 * the terminal and search widgets and adds them here), so this file knows
 * nothing about what is inside a page. Its height is the window's
 * business too (the GtkPaned around it decides it).
 */

#include "config.h"

#include "palang-panel.h"

struct _PalangPanel
{
  AdwBin parent_instance;

  /* Template children */
  AdwViewStack *stack;
};

G_DEFINE_FINAL_TYPE (PalangPanel, palang_panel, ADW_TYPE_BIN)

/* The close button in the top row. */
static void
on_close_clicked (GtkButton   *button,
                  PalangPanel *self)
{
  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
}

void
palang_panel_add_page (PalangPanel *self,
                       GtkWidget   *page,
                       const char  *id,
                       const char  *title,
                       const char  *icon_name)
{
  g_return_if_fail (PALANG_IS_PANEL (self));
  g_return_if_fail (GTK_IS_WIDGET (page));
  g_return_if_fail (id != NULL);

  adw_view_stack_add_titled_with_icon (self->stack, page, id, title, icon_name);
}

void
palang_panel_set_visible_page (PalangPanel *self,
                               const char  *id)
{
  g_return_if_fail (PALANG_IS_PANEL (self));
  g_return_if_fail (id != NULL);

  if (adw_view_stack_get_child_by_name (self->stack, id) != NULL)
    adw_view_stack_set_visible_child_name (self->stack, id);
}

const char *
palang_panel_get_visible_page (PalangPanel *self)
{
  g_return_val_if_fail (PALANG_IS_PANEL (self), NULL);

  return adw_view_stack_get_visible_child_name (self->stack);
}

void
palang_panel_focus_page (PalangPanel *self)
{
  GtkWidget *page;

  g_return_if_fail (PALANG_IS_PANEL (self));

  /* gtk_widget_child_focus () moves the focus to the first widget inside
   * the page that accepts it (the terminal, the search entry, …), which
   * gtk_widget_grab_focus () on the page itself wouldn't do. */
  page = adw_view_stack_get_visible_child (self->stack);
  if (page != NULL)
    gtk_widget_child_focus (page, GTK_DIR_TAB_FORWARD);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_panel_dispose (GObject *object)
{
  gtk_widget_dispose_template (GTK_WIDGET (object), PALANG_TYPE_PANEL);

  G_OBJECT_CLASS (palang_panel_parent_class)->dispose (object);
}

static void
palang_panel_class_init (PalangPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = palang_panel_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-panel.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangPanel, stack);
  gtk_widget_class_bind_template_callback (widget_class, on_close_clicked);
}

static void
palang_panel_init (PalangPanel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

PalangPanel *
palang_panel_new (void)
{
  return g_object_new (PALANG_TYPE_PANEL, NULL);
}
