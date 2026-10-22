/* palang-lsp-languages.c — the tables behind palang-lsp-languages.h.
 *
 * To support another language: add its server to `servers` (if it needs a
 * new one), a "<key>-server" option to the table in palang-settings.c, and
 * its GtkSourceView ids to `languages`. The ids are the `id` attribute in
 * /usr/share/gtksourceview-5/language-specs/<name>.lang.
 */

#include "config.h"

#include "palang-lsp-languages.h"

static const PalangLspServerInfo servers[] = {
  { "c", "C/C++",
    "sudo apt install clangd" },
  /* pylsp finds errors through pyflakes, which apt doesn't always pull
   * in; without it there are no diagnostics. (pycodestyle would add style
   * warnings, left out on purpose.) */
  { "python", "Python",
    "sudo apt install python3-pylsp python3-pyflakes" },
  { "javascript", "JavaScript",
    /* TypeScript 7 (the native rewrite) has no tsserver, which
     * typescript-language-server needs; checked with 6.0.2.
     * Installed with nvm, it is on the PATH only in terminals: Palang
     * started from the desktop menu needs its full path (the client then
     * also finds `node` next to it). */
    "npm install -g typescript-language-server typescript@5\n\n"
    "If it is installed (for example with nvm) and Palang was started from "
    "the desktop menu, give its full path by running this in Palang's terminal:\n"
    "palang set javascript-server \"$(command -v typescript-language-server) --stdio\"" },
};

static const struct {
  const char *source_id;  /* GtkSourceView's language id */
  const char *lsp_id;     /* the protocol's language id */
  const char *server;     /* key in `servers` */
} languages[] = {
  { "c",              "c",               "c" },
  { "chdr",           "c",               "c" },  /* .h files */
  { "cpp",            "cpp",             "c" },
  { "cpphdr",         "cpp",             "c" },  /* .hpp files */
  { "python3",        "python",          "python" },
  { "python",         "python",          "python" },
  { "js",             "javascript",      "javascript" },
  { "jsx",            "javascriptreact", "javascript" },
  { "typescript",     "typescript",      "javascript" },
  { "typescript-jsx", "typescriptreact", "javascript" },
};

static const PalangLspServerInfo *
find_server_by_key (const char *key)
{
  for (guint i = 0; i < G_N_ELEMENTS (servers); i++)
    if (g_str_equal (servers[i].key, key))
      return &servers[i];

  return NULL;
}

const PalangLspServerInfo *
palang_lsp_find_server (const char *source_language_id)
{
  if (source_language_id == NULL)
    return NULL;

  for (guint i = 0; i < G_N_ELEMENTS (languages); i++)
    if (g_str_equal (languages[i].source_id, source_language_id))
      return find_server_by_key (languages[i].server);

  return NULL;
}

const char *
palang_lsp_get_language_id (const char *source_language_id)
{
  if (source_language_id == NULL)
    return NULL;

  for (guint i = 0; i < G_N_ELEMENTS (languages); i++)
    if (g_str_equal (languages[i].source_id, source_language_id))
      return languages[i].lsp_id;

  return NULL;
}

char *
palang_lsp_dup_setting_name (const PalangLspServerInfo *server)
{
  g_return_val_if_fail (server != NULL, NULL);

  return g_strdup_printf ("%s-server", server->key);
}
