/* palang-lsp-problems-panel.h — the "Problems" page of the bottom panel.
 *
 * Lists the errors and warnings of all open files (a GListModel of
 * PalangLspProblem, usually palang_lsp_manager_get_problems ()), one row
 * each: an icon for how bad, the message, where. A click (or Enter, or a
 * double click) emits "problem-activated"; opening the file there is up to
 * whoever listens (the window).
 */

#pragma once

#include <adwaita.h>

#include "palang-lsp-problem.h"

G_BEGIN_DECLS

#define PALANG_TYPE_LSP_PROBLEMS_PANEL (palang_lsp_problems_panel_get_type ())
G_DECLARE_FINAL_TYPE (PalangLspProblemsPanel, palang_lsp_problems_panel,
                      PALANG, LSP_PROBLEMS_PANEL, AdwBin)

/* @problems: a GListModel of PalangLspProblem (referenced; it keeps
 * showing the model's changes).
 * Returns: (transfer floating): a new panel. Signal "problem-activated"
 * (PalangLspProblem *problem). */
PalangLspProblemsPanel *palang_lsp_problems_panel_new   (GListModel             *problems);

/* Moves keyboard focus into the list (to its first row). */
void                    palang_lsp_problems_panel_focus (PalangLspProblemsPanel *self);

G_END_DECLS
