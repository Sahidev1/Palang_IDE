/* palang-lsp-hover-provider.c — popups when the pointer rests on text.
 *
 * Implementing a GObject *interface*: G_DEFINE_FINAL_TYPE_WITH_CODE with
 * G_IMPLEMENT_INTERFACE registers that our type provides
 * GtkSourceHoverProvider, and hover_provider_iface_init fills in the
 * interface's function pointers (here just "populate"). GtkSourceView
 * then calls them like virtual methods.
 */

#include "config.h"

#include "palang-lsp-hover-provider.h"

struct _PalangLspHoverProvider
{
  GObject parent_instance;

  /* A weak pointer: GLib sets it to NULL when the manager is finalized,
   * so we never use a manager that is gone. */
  PalangLspManager *manager;
};

static void hover_provider_iface_init (GtkSourceHoverProviderInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (PalangLspHoverProvider, palang_lsp_hover_provider, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (GTK_SOURCE_TYPE_HOVER_PROVIDER,
                                                      hover_provider_iface_init))

/* Called by GtkSourceView with the position under the pointer (@context)
 * and a place to put widgets (@display). Returning FALSE (with @error set)
 * means "nothing to show"; if no provider shows anything, no popup
 * appears. */
static gboolean
palang_lsp_hover_provider_populate (GtkSourceHoverProvider  *provider,
                                    GtkSourceHoverContext   *context,
                                    GtkSourceHoverDisplay   *display,
                                    GError                 **error)
{
  PalangLspHoverProvider *self = PALANG_LSP_HOVER_PROVIDER (provider);
  GtkSourceBuffer *buffer = gtk_source_hover_context_get_buffer (context);
  PalangLspDocument *lsp_document = NULL;
  g_auto (GStrv) messages = NULL;
  g_autofree char *text = NULL;
  GtkTextIter iter;
  GtkWidget *label;

  if (self->manager != NULL && PALANG_IS_DOCUMENT (buffer))
    lsp_document = palang_lsp_manager_get_lsp_document (self->manager, PALANG_DOCUMENT (buffer));

  if (lsp_document != NULL && gtk_source_hover_context_get_iter (context, &iter))
    messages = palang_lsp_document_dup_messages_at (lsp_document, &iter);

  if (messages == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Nothing to show here");
      return FALSE;
    }

  text = g_strjoinv ("\n", messages);
  label = gtk_label_new (text);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 80);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  gtk_widget_set_margin_start (label, 6);
  gtk_widget_set_margin_end (label, 6);
  gtk_widget_set_margin_top (label, 6);
  gtk_widget_set_margin_bottom (label, 6);
  gtk_source_hover_display_append (display, label);

  return TRUE;
}

static void
hover_provider_iface_init (GtkSourceHoverProviderInterface *iface)
{
  iface->populate = palang_lsp_hover_provider_populate;
}

static void
palang_lsp_hover_provider_dispose (GObject *object)
{
  PalangLspHoverProvider *self = PALANG_LSP_HOVER_PROVIDER (object);

  g_clear_weak_pointer (&self->manager);

  G_OBJECT_CLASS (palang_lsp_hover_provider_parent_class)->dispose (object);
}

static void
palang_lsp_hover_provider_class_init (PalangLspHoverProviderClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = palang_lsp_hover_provider_dispose;
}

static void
palang_lsp_hover_provider_init (PalangLspHoverProvider *self)
{
}

PalangLspHoverProvider *
palang_lsp_hover_provider_new (PalangLspManager *manager)
{
  PalangLspHoverProvider *self;

  g_return_val_if_fail (PALANG_IS_LSP_MANAGER (manager), NULL);

  self = g_object_new (PALANG_TYPE_LSP_HOVER_PROVIDER, NULL);
  g_set_weak_pointer (&self->manager, manager);
  return self;
}
