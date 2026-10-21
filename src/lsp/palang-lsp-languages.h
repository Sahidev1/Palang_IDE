/* palang-lsp-languages.h — which language server serves which language.
 *
 * Plain lookup tables, no objects: GtkSourceView's language ids ("c",
 * "python3", "js", …) are mapped to
 *
 *   - a *server*: one language server program can serve several languages
 *     (clangd: C and C++). Each server has a key ("c", "python",
 *     "javascript"); the command that starts it is the setting
 *     "<key>-server" (PalangSettings, group [lsp]).
 *   - the LSP language id that the protocol uses for the document
 *     ("c", "cpp", "python", "javascript", …).
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  const char *key;           /* "python"; its setting is "python-server" */
  const char *title;         /* "Python", for messages */
  const char *install_hint;  /* how to install the default server */
} PalangLspServerInfo;

/* Returns: (transfer none) (nullable): the server for the GtkSourceView
 * language @source_language_id, or NULL if Palang has none for it. */
const PalangLspServerInfo *palang_lsp_find_server (const char *source_language_id);

/* Returns: (transfer none) (nullable): the LSP language id for the
 * GtkSourceView language @source_language_id ("python3" → "python"), or
 * NULL if Palang has no server for it. */
const char *palang_lsp_get_language_id (const char *source_language_id);

/* Returns: (transfer full): the name of the setting that holds @server's
 * command, "<key>-server". */
char *palang_lsp_dup_setting_name (const PalangLspServerInfo *server);

G_END_DECLS
