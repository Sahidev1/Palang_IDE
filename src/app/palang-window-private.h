/* palang-window-private.h — shared by the files that make up PalangWindow.
 *
 * PalangWindow is one GObject type, but its code is split over several .c
 * files so that no single file grows too big (ARCHITECTURE.md rule 4):
 *
 *   palang-window.c          tabs, opening, saving, closing, session,
 *                            GObject boilerplate
 *   palang-window-panel.c    the bottom panel, its Terminal and Search
 *                            pages, and handing keys to the terminal
 *   palang-window-palette.c  the command palette and Quick Open
 *
 * This header gives those files the window's struct and the few functions
 * they call in each other. It is *private*: only palang-window*.c include
 * it; everything else uses palang-window.h. (GNOME Text Editor splits its
 * window the same way.)
 *
 * Functions declared here start with an underscore, "_palang_window_…", a
 * common C convention for "internal, not part of the public interface".
 */

#pragma once

#include "palang-file-tree.h"
#include "palang-palette.h"
#include "palang-panel.h"
#include "palang-search-panel.h"
#include "palang-terminal.h"
#include "palang-window.h"

G_BEGIN_DECLS

struct _PalangWindow
{
  AdwApplicationWindow parent_instance;

  /* Template children */
  AdwOverlaySplitView *split_view;
  AdwWindowTitle  *sidebar_title;
  PalangFileTree  *file_tree;
  AdwWindowTitle  *window_title;
  AdwTabBar       *tab_bar;
  AdwToastOverlay *toast_overlay;
  GtkStack        *stack;
  AdwTabView      *tab_view;
  GtkPaned        *paned;
  PalangPanel     *panel;

  /* Pages of the panel, created in init */
  PalangTerminal    *terminal;
  PalangSearchPanel *search_panel;

  /* The open palette, if any (not owned; see show_palette), and the
   * action that opened it. */
  PalangPalette   *palette;
  const char      *palette_action;

  /* The panel's height while it's hidden, measured like GtkPaned does:
   * from the divider to the bottom (see _palang_window_get_panel_height). */
  int panel_height;

  /* Bindings that show the selected editor's title/path in the header bar.
   * We hold a reference so we can safely unbind them when the selection
   * changes, even if the old editor is already gone. */
  GBinding *title_binding;
  GBinding *subtitle_binding;
  GBinding *window_title_binding;  /* title the taskbar shows */
};

/* palang-window.c */

/* Shows @message briefly at the bottom of the window. */
void _palang_window_show_toast (PalangWindow *self,
                                const char   *message);

/* palang-window-panel.c */

/* Installs win.toggle-panel and win.search-project. Called from class_init. */
void _palang_window_class_init_panel (GtkWidgetClass *widget_class);

/* Adds the key controller that hands keys to the terminal. Called from init
 * *before* the template is set up, so that it runs before the app's
 * shortcuts (see on_window_key_pressed). */
void _palang_window_init_key_routing (PalangWindow *self);

/* Creates the panel's pages and follows the panel being shown and hidden.
 * Called from init, after the template is set up. */
void _palang_window_init_panel (PalangWindow *self);

/* Returns: the panel's height in pixels, also while it is hidden. */
int  _palang_window_get_panel_height (PalangWindow *self);

/* palang-window-palette.c */

/* Installs win.command-palette and win.quick-open. Called from class_init. */
void _palang_window_class_init_palette (GtkWidgetClass *widget_class);

G_END_DECLS
