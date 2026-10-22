/* palang-lsp-hover-provider.c — popups when the pointer rests on text.
 *
 * Implementing a GObject *interface*: G_DEFINE_FINAL_TYPE_WITH_CODE with
 * G_IMPLEMENT_INTERFACE registers that our type provides
 * GtkSourceHoverProvider, and hover_provider_iface_init fills in the
 * interface's function pointers. GtkSourceView then calls them like
 * virtual methods.
 *
 * We fill in the *async* pair (populate_async/populate_finish), because
 * the server's answer takes a moment: the diagnostics under the pointer
 * are added at once, the server's hover text when its answer comes.
 * GtkSourceView shows the popup once every provider has finished, and
 * cancels our request if the pointer moves on first.
 */

#include "config.h"

#include "palang-lsp-hover-provider.h"
#include "palang-lsp-markdown.h"

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

/* One popup being filled, until the server answers (the GTask's data). */
typedef struct {
  GtkSourceHoverDisplay *display;      /* owned: where widgets go */
  gboolean               has_content;  /* something was added */
} Popup;

static void
popup_free (Popup *popup)
{
  g_clear_object (&popup->display);
  g_free (popup);
}

/* Returns: (transfer floating): a label for the popup. Selectable, so
 * a message or a signature can be copied. */
static GtkWidget *
new_label (const char *text,
           gboolean    use_markup)
{
  GtkWidget *label = gtk_label_new (NULL);

  if (use_markup)
    gtk_label_set_markup (GTK_LABEL (label), text);
  else
    gtk_label_set_text (GTK_LABEL (label), text);

  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  /* Long words (a C++ type, a URL) may break anywhere, not overflow. */
  gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 80);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  gtk_widget_set_margin_start (label, 6);
  gtk_widget_set_margin_end (label, 6);
  gtk_widget_set_margin_top (label, 6);
  gtk_widget_set_margin_bottom (label, 6);
  return label;
}

/* Returns: (transfer floating): the server's text, scrollable when it is
 * long (some documentation is pages long). "propagate-natural-height"
 * makes the scrolled window as tall as the text, up to
 * "max-content-height". */
static GtkWidget *
new_scrolled_label (const char *markup)
{
  GtkWidget *scrolled = gtk_scrolled_window_new ();
  GtkScrolledWindow *window = GTK_SCROLLED_WINDOW (scrolled);

  gtk_scrolled_window_set_policy (window, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_width (window, TRUE);
  gtk_scrolled_window_set_propagate_natural_height (window, TRUE);
  gtk_scrolled_window_set_max_content_height (window, 400);
  gtk_scrolled_window_set_child (window, new_label (markup, TRUE));
  return scrolled;
}

static void
return_result (GTask *task)
{
  Popup *popup = g_task_get_task_data (task);

  /* An error means "nothing to show": if no provider shows anything, no
   * popup appears. */
  if (popup->has_content)
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Nothing to show here");
}

static void
on_hover_answered (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr (GTask) task = user_data;
  Popup *popup = g_task_get_task_data (task);
  g_autoptr (GError) error = NULL;
  g_autofree char *markdown = NULL;

  markdown = palang_lsp_manager_hover_finish (PALANG_LSP_MANAGER (source), result, &error);

  /* The pointer moved on: the popup is gone, add nothing. */
  if (g_task_return_error_if_cancelled (task))
    return;

  /* Any other error (no server, a server without hover, the text changed)
   * just means no hover text; the diagnostics may still be there. */
  if (markdown != NULL)
    {
      g_autofree char *markup = palang_lsp_markdown_to_markup (markdown);

      if (popup->has_content)
        gtk_source_hover_display_append (popup->display,
                                         gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
      gtk_source_hover_display_append (popup->display, new_scrolled_label (markup));
      popup->has_content = TRUE;
    }

  return_result (task);
}

/* Called by GtkSourceView with the position under the pointer (@context)
 * and a place to put widgets (@display). */
static void
palang_lsp_hover_provider_populate_async (GtkSourceHoverProvider *provider,
                                          GtkSourceHoverContext  *context,
                                          GtkSourceHoverDisplay  *display,
                                          GCancellable           *cancellable,
                                          GAsyncReadyCallback     callback,
                                          gpointer                user_data)
{
  PalangLspHoverProvider *self = PALANG_LSP_HOVER_PROVIDER (provider);
  GtkSourceBuffer *buffer = gtk_source_hover_context_get_buffer (context);
  g_autoptr (GTask) task = g_task_new (self, cancellable, callback, user_data);
  PalangLspDocument *lsp_document = NULL;
  g_auto (GStrv) messages = NULL;
  Popup *popup;
  GtkTextIter iter;

  popup = g_new0 (Popup, 1);
  popup->display = g_object_ref (display);
  g_task_set_task_data (task, popup, (GDestroyNotify) popup_free);

  if (self->manager == NULL || !PALANG_IS_DOCUMENT (buffer) ||
      !gtk_source_hover_context_get_iter (context, &iter))
    {
      return_result (task);
      return;
    }

  /* The diagnostics first: we have them already. */
  lsp_document = palang_lsp_manager_get_lsp_document (self->manager, PALANG_DOCUMENT (buffer));
  if (lsp_document != NULL)
    messages = palang_lsp_document_dup_messages_at (lsp_document, &iter);

  if (messages != NULL)
    {
      g_autofree char *text = g_strjoinv ("\n", messages);

      gtk_source_hover_display_append (display, new_label (text, FALSE));
      popup->has_content = TRUE;
    }

  /* Then ask the server. The task goes along as user data and comes back
   * in on_hover_answered (). */
  palang_lsp_manager_hover_async (self->manager, PALANG_DOCUMENT (buffer), &iter, cancellable,
                                  on_hover_answered, g_steal_pointer (&task));
}

static gboolean
palang_lsp_hover_provider_populate_finish (GtkSourceHoverProvider  *provider,
                                           GAsyncResult            *result,
                                           GError                 **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
hover_provider_iface_init (GtkSourceHoverProviderInterface *iface)
{
  iface->populate_async = palang_lsp_hover_provider_populate_async;
  iface->populate_finish = palang_lsp_hover_provider_populate_finish;
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
