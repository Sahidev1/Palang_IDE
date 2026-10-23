/* palang-complaints.h — the complaints file: where it is, what goes in it.
 *
 * sahi's complaints about Palang, written while using it, go into one
 * Markdown file that Claude reads at the start of each session. Each
 * complaint is a heading with the time and what it was about, then the
 * text:
 *
 *   ## 2026-10-23 14:05 · src/main.c:42
 *
 *   The tab closes when I middle-click the editor.
 *
 * Used by the complaint box (palang-complaint-dialog.h) and by
 * `palang complain …` in the Palang terminal.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* @path: the `complaints-file` setting. Empty means complaints.md in
 * Palang's data folder (~/.local/share/palang/); "~/…" is the home folder;
 * a relative path is taken from the home folder too.
 *
 * Returns: (transfer full): the file complaints go to. */
GFile *palang_complaints_get_file    (const char *path);

/* @time: when the complaint was made (local time is shown).
 * @context: (nullable): what it is about, e.g. "src/main.c:42"; NULL for
 * nothing.
 * @text: the complaint; surrounding blank space is dropped.
 *
 * Returns: (transfer full): the entry as Markdown, starting with a blank
 * line and ending with a newline. */
char  *palang_complaints_format_entry (GDateTime  *time,
                                       const char *context,
                                       const char *text);

/* Adds @entry at the end of @file. A new file (and its folders) is created
 * first, with a short heading that says what the file is.
 *
 * Synchronous: it is one small write to a local file, like saving the
 * settings (palang-settings.c), so it doesn't hold up the UI noticeably.
 *
 * Returns: TRUE on success; FALSE with @error set otherwise. */
gboolean palang_complaints_append    (GFile       *file,
                                      const char  *entry,
                                      GError     **error);

G_END_DECLS
