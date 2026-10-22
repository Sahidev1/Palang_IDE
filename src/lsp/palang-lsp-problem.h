/* palang-lsp-problem.h — one error or warning, for the Problems list.
 *
 * A snapshot of one diagnostic: which file, where (taken when the server
 * sent it, or when the list was last rebuilt), how bad, and the message.
 * PalangLspManager keeps a list of these for all open documents
 * (palang_lsp_manager_get_problems ()); PalangLspProblemsPanel shows it.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* The protocol's DiagnosticSeverity numbers. */
typedef enum {
  PALANG_LSP_SEVERITY_ERROR       = 1,
  PALANG_LSP_SEVERITY_WARNING     = 2,
  PALANG_LSP_SEVERITY_INFORMATION = 3,
  PALANG_LSP_SEVERITY_HINT        = 4,
} PalangLspSeverity;

#define PALANG_TYPE_LSP_PROBLEM (palang_lsp_problem_get_type ())
G_DECLARE_FINAL_TYPE (PalangLspProblem, palang_lsp_problem, PALANG, LSP_PROBLEM, GObject)

/* @line, @column: where it starts, counted from 0, the column in
 * characters. @source: (nullable): who found it ("clang", "pyflakes").
 * Returns: (transfer full): a new problem. */
PalangLspProblem *palang_lsp_problem_new          (GFile             *file,
                                                   guint              line,
                                                   guint              column,
                                                   PalangLspSeverity  severity,
                                                   const char        *message,
                                                   const char        *source);

/* Returns: (transfer none): the file it is in. */
GFile            *palang_lsp_problem_get_file     (PalangLspProblem *self);
guint             palang_lsp_problem_get_line     (PalangLspProblem *self);
guint             palang_lsp_problem_get_column   (PalangLspProblem *self);
PalangLspSeverity palang_lsp_problem_get_severity (PalangLspProblem *self);
/* Returns: (transfer none): the server's message (may hold newlines). */
const char       *palang_lsp_problem_get_message  (PalangLspProblem *self);
/* Returns: (transfer none) (nullable): who found it. */
const char       *palang_lsp_problem_get_source   (PalangLspProblem *self);

/* Returns: <0 if @a comes before @b in the list (like strcmp): by file
 * (its URI), then line, then column. */
int               palang_lsp_problem_compare      (PalangLspProblem *a,
                                                   PalangLspProblem *b);

G_END_DECLS
