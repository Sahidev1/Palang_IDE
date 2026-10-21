/* palang-lsp-document.c — one open document, as a language server sees it.
 *
 * Incremental changes are sent from the buffer's "insert-text" and
 * "delete-range" signals, connected *before* GTK applies the change (a
 * plain g_signal_connect runs before the default handler of these
 * RUN_LAST signals). At that moment the iterators still describe the old
 * text, which is exactly what the protocol wants: a range in the text as
 * the server has it, and what replaces it. Full-text sync instead waits
 * for "changed", after the edit.
 *
 * Messages go out in order (jsonrpc-glib queues them), so the server sees
 * the edits in the order they happened.
 */

#include "config.h"

#include <jsonrpc-glib.h>

#include "palang-lsp-document.h"
#include "palang-lsp-languages.h"
#include "palang-lsp-position.h"

struct _PalangLspDocument
{
  GObject parent_instance;

  PalangDocument  *document;  /* owned */
  PalangLspClient *client;    /* owned, nullable */

  /* While the server has the document open: the URI and language it was
   * opened with. NULL when it isn't open. */
  char *open_uri;
  char *open_language_id;

  gint64 version;  /* last version sent; only ever grows */
};

G_DEFINE_FINAL_TYPE (PalangLspDocument, palang_lsp_document, G_TYPE_OBJECT)

/* ---- Opening and closing ------------------------------------------------ */

/* Returns: (transfer full) (nullable): the LSP language id of the
 * document's current language, NULL if no server handles it. */
static char *
dup_language_id (PalangLspDocument *self)
{
  GtkSourceLanguage *language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (self->document));

  if (language == NULL)
    return NULL;

  return g_strdup (palang_lsp_get_language_id (gtk_source_language_get_id (language)));
}

/* Returns: (transfer full) (nullable): the URI of the document's file. */
static char *
dup_file_uri (PalangLspDocument *self)
{
  GFile *file = palang_document_get_file (self->document);

  return file != NULL ? g_file_get_uri (file) : NULL;
}

static char *
dup_all_text (PalangLspDocument *self)
{
  GtkTextIter start, end;

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self->document), &start, &end);
  /* A slice includes hidden text; see palang_lsp_position_from_iter. */
  return gtk_text_iter_get_slice (&start, &end);
}

/* "textDocument/didOpen", if everything is in place: a ready server, a
 * file to name the document by, and a language the server knows. */
static void
open_document (PalangLspDocument *self)
{
  g_autofree char *uri = dup_file_uri (self);
  g_autofree char *language_id = dup_language_id (self);
  g_autofree char *text = NULL;
  g_autoptr (GVariant) params = NULL;

  if (self->open_uri != NULL || self->client == NULL ||
      !palang_lsp_client_is_ready (self->client) || uri == NULL || language_id == NULL)
    return;

  text = dup_all_text (self);
  self->version++;
  params = JSONRPC_MESSAGE_NEW (
    "textDocument", "{",
      "uri", JSONRPC_MESSAGE_PUT_STRING (uri),
      "languageId", JSONRPC_MESSAGE_PUT_STRING (language_id),
      "version", JSONRPC_MESSAGE_PUT_INT64 (self->version),
      "text", JSONRPC_MESSAGE_PUT_STRING (text),
    "}");
  palang_lsp_client_notify (self->client, "textDocument/didOpen", params);

  self->open_uri = g_steal_pointer (&uri);
  self->open_language_id = g_steal_pointer (&language_id);
}

/* "textDocument/didClose", if the server has the document open. */
static void
close_document (PalangLspDocument *self)
{
  g_autoptr (GVariant) params = NULL;

  if (self->open_uri == NULL)
    return;

  params = JSONRPC_MESSAGE_NEW (
    "textDocument", "{",
      "uri", JSONRPC_MESSAGE_PUT_STRING (self->open_uri),
    "}");
  palang_lsp_client_notify (self->client, "textDocument/didClose", params);

  g_clear_pointer (&self->open_uri, g_free);
  g_clear_pointer (&self->open_language_id, g_free);
}

/* The server (re)started: it knows nothing yet, so whatever we thought
 * was open isn't. Open it with the current text. */
static void
on_client_ready (PalangLspClient   *client,
                 PalangLspDocument *self)
{
  g_clear_pointer (&self->open_uri, g_free);
  g_clear_pointer (&self->open_language_id, g_free);
  open_document (self);
}

void
palang_lsp_document_set_client (PalangLspDocument *self,
                                PalangLspClient   *client)
{
  g_return_if_fail (PALANG_IS_LSP_DOCUMENT (self));
  g_return_if_fail (client == NULL || PALANG_IS_LSP_CLIENT (client));

  if (client == self->client)
    {
      g_autofree char *uri = dup_file_uri (self);
      g_autofree char *language_id = dup_language_id (self);

      /* Same server: re-open only if the name or language changed. */
      if (g_strcmp0 (uri, self->open_uri) != 0 ||
          g_strcmp0 (language_id, self->open_language_id) != 0)
        {
          close_document (self);
          open_document (self);
        }
      return;
    }

  close_document (self);
  if (self->client != NULL)
    g_signal_handlers_disconnect_by_data (self->client, self);

  g_set_object (&self->client, client);

  if (client != NULL)
    {
      g_signal_connect (client, "ready", G_CALLBACK (on_client_ready), self);
      open_document (self);
    }
}

/* ---- Following the document --------------------------------------------- */

/* One change in "contentChanges": the range [start, end) of the old text
 * is replaced by @text. */
static void
send_range_change (PalangLspDocument *self,
                   const GtkTextIter *start,
                   const GtkTextIter *end,
                   const char        *text)
{
  guint start_line, start_character, end_line, end_character;
  g_autoptr (GVariant) params = NULL;

  palang_lsp_position_from_iter (start, &start_line, &start_character);
  palang_lsp_position_from_iter (end, &end_line, &end_character);

  self->version++;
  params = JSONRPC_MESSAGE_NEW (
    "textDocument", "{",
      "uri", JSONRPC_MESSAGE_PUT_STRING (self->open_uri),
      "version", JSONRPC_MESSAGE_PUT_INT64 (self->version),
    "}",
    "contentChanges", "[", "{",
      "range", "{",
        "start", "{",
          "line", JSONRPC_MESSAGE_PUT_INT64 (start_line),
          "character", JSONRPC_MESSAGE_PUT_INT64 (start_character),
        "}",
        "end", "{",
          "line", JSONRPC_MESSAGE_PUT_INT64 (end_line),
          "character", JSONRPC_MESSAGE_PUT_INT64 (end_character),
        "}",
      "}",
      "text", JSONRPC_MESSAGE_PUT_STRING (text),
    "}", "]");
  palang_lsp_client_notify (self->client, "textDocument/didChange", params);
}

static gboolean
wants_sync (PalangLspDocument *self,
            PalangLspSyncKind  kind)
{
  return self->open_uri != NULL &&
         palang_lsp_client_get_sync_kind (self->client) == kind;
}

/* Text is about to be inserted at @location (@length bytes of @text, not
 * necessarily NUL-terminated). */
static void
on_insert_text (GtkTextBuffer     *buffer,
                GtkTextIter       *location,
                const char        *text,
                int                length,
                PalangLspDocument *self)
{
  g_autofree char *inserted = NULL;

  if (!wants_sync (self, PALANG_LSP_SYNC_INCREMENTAL))
    return;

  inserted = g_strndup (text, length);
  send_range_change (self, location, location, inserted);
}

/* The text between @start and @end is about to be deleted. */
static void
on_delete_range (GtkTextBuffer     *buffer,
                 GtkTextIter       *start,
                 GtkTextIter       *end,
                 PalangLspDocument *self)
{
  if (!wants_sync (self, PALANG_LSP_SYNC_INCREMENTAL))
    return;

  send_range_change (self, start, end, "");
}

/* After any edit: for servers that want the whole text every time. */
static void
on_changed (GtkTextBuffer     *buffer,
            PalangLspDocument *self)
{
  g_autofree char *text = NULL;
  g_autoptr (GVariant) params = NULL;

  if (!wants_sync (self, PALANG_LSP_SYNC_FULL))
    return;

  text = dup_all_text (self);
  self->version++;
  params = JSONRPC_MESSAGE_NEW (
    "textDocument", "{",
      "uri", JSONRPC_MESSAGE_PUT_STRING (self->open_uri),
      "version", JSONRPC_MESSAGE_PUT_INT64 (self->version),
    "}",
    "contentChanges", "[", "{",
      "text", JSONRPC_MESSAGE_PUT_STRING (text),
    "}", "]");
  palang_lsp_client_notify (self->client, "textDocument/didChange", params);
}

/* The document got a file (first save) or another one (Save As). The
 * signal also comes on every plain save; set_client sorts that out. */
static void
on_file_changed (PalangDocument    *document,
                 GParamSpec        *pspec,
                 PalangLspDocument *self)
{
  palang_lsp_document_set_client (self, self->client);
}

static void
on_saved (PalangDocument    *document,
          PalangLspDocument *self)
{
  g_autoptr (GVariant) params = NULL;

  if (self->open_uri == NULL)
    return;

  params = JSONRPC_MESSAGE_NEW (
    "textDocument", "{",
      "uri", JSONRPC_MESSAGE_PUT_STRING (self->open_uri),
    "}");
  palang_lsp_client_notify (self->client, "textDocument/didSave", params);
}

/* ---- Getters ------------------------------------------------------------ */

PalangDocument *
palang_lsp_document_get_document (PalangLspDocument *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_DOCUMENT (self), NULL);

  return self->document;
}

PalangLspClient *
palang_lsp_document_get_client (PalangLspDocument *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_DOCUMENT (self), NULL);

  return self->client;
}

gboolean
palang_lsp_document_is_open (PalangLspDocument *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_DOCUMENT (self), FALSE);

  return self->open_uri != NULL;
}

const char *
palang_lsp_document_get_uri (PalangLspDocument *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_DOCUMENT (self), NULL);

  return self->open_uri;
}

gint64
palang_lsp_document_get_version (PalangLspDocument *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_DOCUMENT (self), 0);

  return self->version;
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_lsp_document_dispose (GObject *object)
{
  PalangLspDocument *self = PALANG_LSP_DOCUMENT (object);

  /* Closes the document on its server. */
  palang_lsp_document_set_client (self, NULL);

  if (self->document != NULL)
    g_signal_handlers_disconnect_by_data (self->document, self);
  g_clear_object (&self->document);

  G_OBJECT_CLASS (palang_lsp_document_parent_class)->dispose (object);
}

static void
palang_lsp_document_class_init (PalangLspDocumentClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = palang_lsp_document_dispose;
}

static void
palang_lsp_document_init (PalangLspDocument *self)
{
}

PalangLspDocument *
palang_lsp_document_new (PalangDocument *document)
{
  PalangLspDocument *self;

  g_return_val_if_fail (PALANG_IS_DOCUMENT (document), NULL);

  self = g_object_new (PALANG_TYPE_LSP_DOCUMENT, NULL);
  self->document = g_object_ref (document);

  g_signal_connect (document, "insert-text", G_CALLBACK (on_insert_text), self);
  g_signal_connect (document, "delete-range", G_CALLBACK (on_delete_range), self);
  g_signal_connect (document, "changed", G_CALLBACK (on_changed), self);
  g_signal_connect (document, "notify::file", G_CALLBACK (on_file_changed), self);
  g_signal_connect (document, "saved", G_CALLBACK (on_saved), self);

  return self;
}
