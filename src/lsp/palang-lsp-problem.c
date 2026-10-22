/* palang-lsp-problem.c — one error or warning, for the Problems list (see
 * the header). A plain value object: set once, read many times.
 */

#include "config.h"

#include "palang-lsp-problem.h"

struct _PalangLspProblem
{
  GObject parent_instance;

  GFile             *file;     /* owned */
  char              *uri;      /* owned: of file, for sorting */
  guint              line;
  guint              column;
  PalangLspSeverity  severity;
  char              *message;  /* owned */
  char              *source;   /* owned, nullable */
};

G_DEFINE_FINAL_TYPE (PalangLspProblem, palang_lsp_problem, G_TYPE_OBJECT)

PalangLspProblem *
palang_lsp_problem_new (GFile             *file,
                        guint              line,
                        guint              column,
                        PalangLspSeverity  severity,
                        const char        *message,
                        const char        *source)
{
  PalangLspProblem *self;

  g_return_val_if_fail (G_IS_FILE (file), NULL);
  g_return_val_if_fail (message != NULL, NULL);

  self = g_object_new (PALANG_TYPE_LSP_PROBLEM, NULL);
  self->file = g_object_ref (file);
  self->uri = g_file_get_uri (file);
  self->line = line;
  self->column = column;
  self->severity = severity;
  self->message = g_strdup (message);
  self->source = g_strdup (source);
  return self;
}

GFile *
palang_lsp_problem_get_file (PalangLspProblem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_PROBLEM (self), NULL);

  return self->file;
}

guint
palang_lsp_problem_get_line (PalangLspProblem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_PROBLEM (self), 0);

  return self->line;
}

guint
palang_lsp_problem_get_column (PalangLspProblem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_PROBLEM (self), 0);

  return self->column;
}

PalangLspSeverity
palang_lsp_problem_get_severity (PalangLspProblem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_PROBLEM (self), PALANG_LSP_SEVERITY_ERROR);

  return self->severity;
}

const char *
palang_lsp_problem_get_message (PalangLspProblem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_PROBLEM (self), NULL);

  return self->message;
}

const char *
palang_lsp_problem_get_source (PalangLspProblem *self)
{
  g_return_val_if_fail (PALANG_IS_LSP_PROBLEM (self), NULL);

  return self->source;
}

int
palang_lsp_problem_compare (PalangLspProblem *a,
                            PalangLspProblem *b)
{
  int by_file;

  g_return_val_if_fail (PALANG_IS_LSP_PROBLEM (a), 0);
  g_return_val_if_fail (PALANG_IS_LSP_PROBLEM (b), 0);

  by_file = g_strcmp0 (a->uri, b->uri);
  if (by_file != 0)
    return by_file;
  if (a->line != b->line)
    return a->line < b->line ? -1 : 1;
  if (a->column != b->column)
    return a->column < b->column ? -1 : 1;
  return 0;
}

static void
palang_lsp_problem_finalize (GObject *object)
{
  PalangLspProblem *self = PALANG_LSP_PROBLEM (object);

  g_clear_object (&self->file);
  g_free (self->uri);
  g_free (self->message);
  g_free (self->source);

  G_OBJECT_CLASS (palang_lsp_problem_parent_class)->finalize (object);
}

static void
palang_lsp_problem_class_init (PalangLspProblemClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = palang_lsp_problem_finalize;
}

static void
palang_lsp_problem_init (PalangLspProblem *self)
{
}
