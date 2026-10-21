/* palang-window.h — the main window: tabs of open documents, the folder
 * sidebar and the bottom panel.
 *
 * Property "active-editor" (PalangEditor, read-only, nullable): the editor
 * in the selected tab. Listen to "notify::active-editor" to follow it. */

#pragma once

#include <adwaita.h>

#include "palang-application.h"
#include "palang-editor.h"
#include "palang-panel.h"
#include "palang-session.h"
#include "palang-terminal.h"

G_BEGIN_DECLS

#define PALANG_TYPE_WINDOW (palang_window_get_type ())

G_DECLARE_FINAL_TYPE (PalangWindow, palang_window,
                      PALANG, WINDOW, AdwApplicationWindow)

PalangWindow *palang_window_new               (PalangApplication *application);

/* Opens @file in a new tab, or switches to its tab if already open.
 * Loading happens in the background; errors are shown as a toast. */
void          palang_window_open_file         (PalangWindow *self,
                                               GFile        *file);

/* Shows @folder in the sidebar's folder tree. */
void          palang_window_open_folder       (PalangWindow *self,
                                               GFile        *folder);

/* Returns: (transfer none) (nullable): the folder shown in the sidebar. */
GFile        *palang_window_get_folder        (PalangWindow *self);

/* Opens a new, empty "Untitled" document in a new tab. */
void          palang_window_new_document      (PalangWindow *self);

/* Returns: (transfer none) (nullable): the editor in the selected tab. */
PalangEditor *palang_window_get_active_editor (PalangWindow *self);

guint         palang_window_get_n_editors     (PalangWindow *self);

/* Returns: (transfer none): the bottom panel under the tabs. */
PalangPanel  *palang_window_get_panel         (PalangWindow *self);

/* Returns: (transfer none): the terminal, a page of the panel. */
PalangTerminal *palang_window_get_terminal    (PalangWindow *self);

/* Returns: (transfer full): what's open now, for saving (see
 * palang-session.h). Never-saved documents are left out. */
PalangSession *palang_window_dup_session      (PalangWindow *self);

/* Opens the folder and files of @session, in order, and selects the tab
 * that was active. Missing files and folders are skipped. */
void          palang_window_restore_session   (PalangWindow        *self,
                                               const PalangSession *session);

G_END_DECLS
