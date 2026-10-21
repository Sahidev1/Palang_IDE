/* palang-lsp-client.h — one running language server.
 *
 * A PalangLspClient starts a language server program (clangd, pylsp, …),
 * talks to it with JSON-RPC over its stdin/stdout, and does the protocol's
 * start and end: the "initialize" handshake, and "shutdown" + "exit" when
 * stopping. If the server crashes it is started again, up to three times a
 * minute; after that the client gives up and emits "failed".
 *
 * Messages are GVariants, as jsonrpc-glib uses them: a JSON object is an
 * a{sv} dictionary. JSONRPC_MESSAGE_NEW () builds one (see jsonrpc-glib's
 * jsonrpc-message.h).
 *
 * Signals:
 *   "ready"         the server has been initialized; requests can be sent.
 *                   Emitted again after each restart, so whoever sent it
 *                   documents should send them again then.
 *   "failed"        (const char *message): the server can't be started or
 *                   kept running; @message is ready to show to the user.
 *   "notification"  (const char *method, GVariant *params): a notification
 *                   from the server, e.g. "textDocument/publishDiagnostics".
 *
 * Usually created by PalangLspManager, not directly.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define PALANG_TYPE_LSP_CLIENT (palang_lsp_client_get_type ())
G_DECLARE_FINAL_TYPE (PalangLspClient, palang_lsp_client, PALANG, LSP_CLIENT, GObject)

/* @title: what the server is for ("Python"), used in messages.
 * @argv: the command that starts the server.
 * @root: (nullable): the project folder, NULL if none is open.
 * @install_hint: (nullable): how to install the server, shown when the
 * program isn't found ("sudo apt install python3-pylsp").
 *
 * Returns: (transfer full): a client; call palang_lsp_client_start (). */
PalangLspClient *palang_lsp_client_new (const char        *title,
                                        const char * const *argv,
                                        GFile             *root,
                                        const char        *install_hint);

/* Starts the server, unless it is running already. Doesn't wait: "ready"
 * or "failed" follows. */
void        palang_lsp_client_start            (PalangLspClient *self);

/* Asks the server to shut down and exit (the polite way the protocol
 * describes), and kills it if it hasn't exited after two seconds. The
 * client keeps itself alive until then, so it can be unreffed right after
 * this call. While it runs, the application is held, so quitting Palang
 * waits for it. */
void        palang_lsp_client_stop             (PalangLspClient *self);

/* Returns: TRUE if the server has been initialized and is running. */
gboolean    palang_lsp_client_is_ready         (PalangLspClient *self);

/* Returns: (transfer none): what the server is for, like "Python". */
const char *palang_lsp_client_get_title        (PalangLspClient *self);

/* Returns: (transfer none) (nullable): the "capabilities" the server
 * announced in its reply to "initialize", NULL until it is ready. */
GVariant   *palang_lsp_client_get_capabilities (PalangLspClient *self);

/* Sends the request @method with @params (transfer none, nullable). Fails
 * with G_IO_ERROR_NOT_CONNECTED if the server isn't ready. */
void        palang_lsp_client_call_async       (PalangLspClient     *self,
                                                const char          *method,
                                                GVariant            *params,
                                                GCancellable        *cancellable,
                                                GAsyncReadyCallback  callback,
                                                gpointer             user_data);

/* Returns: (transfer full) (nullable): the server's reply, NULL on error or
 * if the reply is JSON null. Check @error to tell them apart. */
GVariant   *palang_lsp_client_call_finish      (PalangLspClient  *self,
                                                GAsyncResult     *result,
                                                GError          **error);

/* Sends the notification @method with @params (transfer none, nullable).
 * Does nothing if the server isn't ready. */
void        palang_lsp_client_notify           (PalangLspClient *self,
                                                const char      *method,
                                                GVariant        *params);

G_END_DECLS
