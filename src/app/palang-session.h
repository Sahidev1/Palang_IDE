/* palang-session.h — what was open when Palang closed.
 *
 * Saved when the window closes and restored when Palang starts without
 * arguments: the open folder, the open files (in tab order), which tab was
 * selected, whether the sidebar was shown, and the bottom panel (shown or
 * not, its height and its page). Never-saved "Untitled"
 * documents are not part of it.
 *
 * Stored in ~/.local/state/palang/session.ini:
 *
 *   [session]
 *   folder=file:///home/ali/projects/foo
 *   files=file:///home/ali/projects/foo/main.c;file:///…/util.c;
 *   active=1
 *   show-sidebar=true
 *   show-panel=true
 *   panel-height=250
 *   panel-page=terminal
 *
 * Locations are stored as URIs, so names with unusual characters survive.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct {
  GFile     *folder;        /* nullable */
  GPtrArray *files;         /* GFile, in tab order */
  int        active;        /* index into files, or -1 */
  gboolean   show_sidebar;
  gboolean   show_panel;
  int        panel_height;  /* pixels; 0 means "the default" */
  char      *panel_page;    /* nullable: a page id, e.g. "terminal" */
} PalangSession;

PalangSession *palang_session_new          (void);
void           palang_session_free         (PalangSession *session);

/* Returns: (transfer full): ~/.local/state/palang/session.ini */
char          *palang_session_default_path (void);

gboolean       palang_session_save         (const PalangSession  *session,
                                            const char           *path,
                                            GError              **error);

/* Returns: (transfer full) (nullable): the saved session, or NULL if there
 * is none or it can't be read. */
PalangSession *palang_session_load         (const char *path);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PalangSession, palang_session_free)

G_END_DECLS
