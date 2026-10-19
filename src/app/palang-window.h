/* palang-window.h — the main window type. */

#pragma once

#include <adwaita.h>

#include "palang-application.h"

G_BEGIN_DECLS

#define PALANG_TYPE_WINDOW (palang_window_get_type ())

G_DECLARE_FINAL_TYPE (PalangWindow, palang_window,
                      PALANG, WINDOW, AdwApplicationWindow)

PalangWindow *palang_window_new (PalangApplication *application);

G_END_DECLS
