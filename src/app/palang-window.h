/* palang-window.h — the main window: tabs of open documents. */

#pragma once

#include <adwaita.h>

#include "palang-application.h"
#include "palang-editor.h"

G_BEGIN_DECLS

#define PALANG_TYPE_WINDOW (palang_window_get_type ())

G_DECLARE_FINAL_TYPE (PalangWindow, palang_window,
                      PALANG, WINDOW, AdwApplicationWindow)

PalangWindow *palang_window_new               (PalangApplication *application);

/* Opens @file in a new tab, or switches to its tab if already open.
 * Loading happens in the background; errors are shown as a toast. */
void          palang_window_open_file         (PalangWindow *self,
                                               GFile        *file);

/* Opens a new, empty "Untitled" document in a new tab. */
void          palang_window_new_document      (PalangWindow *self);

/* Returns: (transfer none) (nullable): the editor in the selected tab. */
PalangEditor *palang_window_get_active_editor (PalangWindow *self);

guint         palang_window_get_n_editors     (PalangWindow *self);

G_END_DECLS
