/* palang-search.c — runs ripgrep and collects its matches (see the header).
 *
 * Each search is a "run": rg as a subprocess (GSubprocess), whose output
 * we read one line at a time, asynchronously, so the window stays
 * responsive however long rg takes. A run lives in a small struct that
 * travels through the callbacks:
 *
 *   start → read a line → parse it → read the next line → … → end of
 *   output → wait for rg to exit → "finished"
 *
 * Starting a new search cancels the run's GCancellable: its next callback
 * sees that, frees the run and stops, so an old search can never add
 * results to a new one.
 */

#include "config.h"

#include "palang-search.h"

/* Matches are added to the list in batches: adding them one by one would
 * make the list widget update thousands of times. */
#define BATCH_SIZE 100

typedef struct {
  PalangSearch     *search;       /* owned */
  GFile            *folder;       /* owned */
  GSubprocess      *process;      /* owned */
  GDataInputStream *output;       /* owned: rg's output, read line by line */
  GCancellable     *cancellable;  /* owned */
  GPtrArray        *pending;      /* matches not yet in the list */
  guint             n_matches;
} Run;

struct _PalangSearch
{
  GObject parent_instance;

  GListStore *results;  /* PalangSearchMatch */
  Run        *run;      /* the running search, or NULL; not owned (see Run) */
};

G_DEFINE_FINAL_TYPE (PalangSearch, palang_search, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_RUNNING,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum {
  SIGNAL_FINISHED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ---- Runs --------------------------------------------------------------- */

static void
run_free (Run *run)
{
  /* Stops rg if it's still going; does nothing if it already exited. */
  g_subprocess_force_exit (run->process);

  g_clear_object (&run->search);
  g_clear_object (&run->folder);
  g_clear_object (&run->process);
  g_clear_object (&run->output);
  g_clear_object (&run->cancellable);
  g_clear_pointer (&run->pending, g_ptr_array_unref);
  g_free (run);
}

/* Moves the pending matches into the results list, in one go. */
static void
flush_pending (Run *run)
{
  GListStore *results = run->search->results;

  if (run->pending->len == 0)
    return;

  g_list_store_splice (results, g_list_model_get_n_items (G_LIST_MODEL (results)), 0,
                       run->pending->pdata, run->pending->len);
  g_ptr_array_set_size (run->pending, 0);
}

static void
set_run (PalangSearch *self,
         Run          *run)
{
  gboolean was_running = self->run != NULL;

  self->run = run;
  if (was_running != (run != NULL))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);
}

/* The run ended normally: tell everyone and clean up. */
static void
finish_run (Run        *run,
            gboolean    truncated,
            const char *error_message)
{
  PalangSearch *self = run->search;

  flush_pending (run);
  if (self->run == run)
    set_run (self, NULL);
  g_signal_emit (self, signals[SIGNAL_FINISHED], 0, truncated, error_message);
  run_free (run);
}

/* rg wrote an error message (it exits with status 2). Its last line says
 * what's wrong, e.g. "error: unclosed group" for a bad regex. */
static void
on_errors_read (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
  Run *run = user_data;
  g_autoptr (GBytes) bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source), result, NULL);
  g_autofree char *text = NULL;
  g_auto (GStrv) lines = NULL;
  const char *message = "ripgrep (rg) reported an error.";

  if (g_cancellable_is_cancelled (run->cancellable))
    {
      run_free (run);
      return;
    }

  if (bytes != NULL)
    {
      text = g_utf8_make_valid (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));
      lines = g_strsplit (g_strstrip (text), "\n", -1);
      if (lines[0] != NULL && lines[0][0] != '\0')
        message = lines[g_strv_length (lines) - 1];
    }

  finish_run (run, FALSE, message);
}

static void
on_exited (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
  Run *run = user_data;
  GSubprocess *process = G_SUBPROCESS (source);

  if (!g_subprocess_wait_finish (process, result, NULL) ||
      g_cancellable_is_cancelled (run->cancellable))
    {
      run_free (run);
      return;
    }

  /* rg's exit status: 0 found something, 1 found nothing, 2 error. Errors
   * about single files are silenced (--no-messages), so 2 with no matches
   * means the search itself failed. */
  if (g_subprocess_get_if_exited (process) && g_subprocess_get_exit_status (process) == 2 &&
      run->n_matches == 0)
    {
      g_input_stream_read_bytes_async (g_subprocess_get_stderr_pipe (process), 4096,
                                       G_PRIORITY_DEFAULT, run->cancellable,
                                       on_errors_read, run);
      return;
    }

  finish_run (run, FALSE, NULL);
}

static void read_next_line (Run *run);

static void
on_line_read (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  Run *run = user_data;
  g_autoptr (GError) error = NULL;
  g_autofree char *line = NULL;
  PalangSearchMatch *match;

  line = g_data_input_stream_read_line_finish (G_DATA_INPUT_STREAM (source), result, NULL, &error);

  if (g_cancellable_is_cancelled (run->cancellable))
    {
      run_free (run);
      return;
    }

  if (error != NULL)
    {
      finish_run (run, FALSE, error->message);
      return;
    }

  /* NULL without an error: the end of rg's output. */
  if (line == NULL)
    {
      g_subprocess_wait_async (run->process, run->cancellable, on_exited, run);
      return;
    }

  /* Lines that aren't matches (or that we can't read) are skipped. */
  match = palang_rg_parse_line (line, run->folder, NULL);
  if (match != NULL)
    {
      g_ptr_array_add (run->pending, match);
      run->n_matches++;

      if (run->n_matches >= PALANG_SEARCH_MAX_RESULTS)
        {
          finish_run (run, TRUE, NULL);  /* also stops rg */
          return;
        }
      if (run->pending->len >= BATCH_SIZE)
        flush_pending (run);
    }

  read_next_line (run);
}

static void
read_next_line (Run *run)
{
  g_data_input_stream_read_line_async (run->output, G_PRIORITY_DEFAULT, run->cancellable,
                                       on_line_read, run);
}

/* Abandons the running search: its callbacks will see the cancellation
 * and free it. */
static void
cancel_run (PalangSearch *self)
{
  if (self->run == NULL)
    return;

  g_cancellable_cancel (self->run->cancellable);
  g_subprocess_force_exit (self->run->process);
  set_run (self, NULL);
}

/* ---- Public API --------------------------------------------------------- */

void
palang_search_start (PalangSearch      *self,
                     GFile             *folder,
                     const char        *text,
                     PalangSearchFlags  flags)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) process = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) argv = NULL;
  g_autofree char *folder_path = NULL;
  Run *run;

  g_return_if_fail (PALANG_IS_SEARCH (self));
  g_return_if_fail (folder == NULL || G_IS_FILE (folder));
  g_return_if_fail (text != NULL);

  cancel_run (self);
  g_list_store_remove_all (self->results);

  if (folder == NULL || text[0] == '\0')
    return;

  folder_path = g_file_get_path (folder);
  if (folder_path == NULL)
    {
      g_signal_emit (self, signals[SIGNAL_FINISHED], 0, FALSE,
                     "Only folders on this computer can be searched.");
      return;
    }

  /* rg runs in the folder, so it searches it and prints paths relative to
   * it. Its output and error messages come to us through pipes. */
  argv = palang_rg_build_argv (text, flags);
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                        G_SUBPROCESS_FLAGS_STDERR_PIPE);
  g_subprocess_launcher_set_cwd (launcher, folder_path);
  process = g_subprocess_launcher_spawnv (launcher, (const char * const *) argv, &error);

  if (process == NULL)
    {
      if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        g_signal_emit (self, signals[SIGNAL_FINISHED], 0, FALSE,
                       "Project search needs ripgrep. Install it with: sudo apt install ripgrep");
      else
        g_signal_emit (self, signals[SIGNAL_FINISHED], 0, FALSE, error->message);
      return;
    }

  run = g_new0 (Run, 1);
  run->search = g_object_ref (self);
  run->folder = g_object_ref (folder);
  run->process = g_steal_pointer (&process);
  /* GDataInputStream adds "read a line" on top of the plain pipe. */
  run->output = g_data_input_stream_new (g_subprocess_get_stdout_pipe (run->process));
  run->cancellable = g_cancellable_new ();
  run->pending = g_ptr_array_new_with_free_func (g_object_unref);

  set_run (self, run);
  read_next_line (run);
}

void
palang_search_stop (PalangSearch *self)
{
  g_return_if_fail (PALANG_IS_SEARCH (self));

  if (self->run != NULL)
    flush_pending (self->run);
  cancel_run (self);
}

GListModel *
palang_search_get_results (PalangSearch *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH (self), NULL);

  return G_LIST_MODEL (self->results);
}

gboolean
palang_search_is_running (PalangSearch *self)
{
  g_return_val_if_fail (PALANG_IS_SEARCH (self), FALSE);

  return self->run != NULL;
}

/* ---- Listing files ------------------------------------------------------ */

/* The GTask pattern: an async function creates a GTask (which remembers
 * the callback), starts the work, and later "returns" through the task
 * (g_task_return_*). The _finish function reads the result back out. */

static void
on_files_listed (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr (GTask) task = user_data;
  GSubprocess *process = G_SUBPROCESS (source);
  g_autoptr (GBytes) output = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_autofree char *text = NULL;
  g_auto (GStrv) lines = NULL;

  if (!g_subprocess_communicate_finish (process, result, &output, NULL, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Exit status 1 just means "no files"; 2 is an error. */
  if (g_subprocess_get_if_exited (process) && g_subprocess_get_exit_status (process) == 2)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "ripgrep (rg) could not list the files.");
      return;
    }

  /* One path per line. g_strndup () adds the NUL the bytes don't have.
   * (Empty output has no data at all: the pointer is NULL.) */
  if (g_bytes_get_size (output) > 0)
    {
      text = g_strndup (g_bytes_get_data (output, NULL), g_bytes_get_size (output));
      lines = g_strsplit (text, "\n", -1);
      for (guint i = 0; lines[i] != NULL; i++)
        if (lines[i][0] != '\0')
          g_strv_builder_add (builder, lines[i]);
    }

  g_task_return_pointer (task, g_strv_builder_end (builder), (GDestroyNotify) g_strfreev);
}

void
palang_search_list_files_async (GFile               *folder,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_autoptr (GTask) task = g_task_new (NULL, cancellable, callback, user_data);
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) process = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *folder_path = NULL;
  const char *argv[] = { "rg", "--files", "--no-messages", "--sort", "path", NULL };

  g_return_if_fail (G_IS_FILE (folder));

  folder_path = g_file_get_path (folder);
  if (folder_path == NULL)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "Only folders on this computer can be listed.");
      return;
    }

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                        G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  g_subprocess_launcher_set_cwd (launcher, folder_path);
  process = g_subprocess_launcher_spawnv (launcher, argv, &error);
  if (process == NULL)
    {
      if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                 "Quick Open needs ripgrep. Install it with: sudo apt install ripgrep");
      else
        g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Collects all of rg's output, then calls on_files_listed. The task's
   * reference moves to the callback. */
  g_subprocess_communicate_async (process, NULL, cancellable, on_files_listed,
                                  g_steal_pointer (&task));
}

char **
palang_search_list_files_finish (GAsyncResult  *result,
                                 GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_search_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PalangSearch *self = PALANG_SEARCH (object);

  switch (prop_id)
    {
    case PROP_RUNNING:
      g_value_set_boolean (value, palang_search_is_running (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_search_finalize (GObject *object)
{
  PalangSearch *self = PALANG_SEARCH (object);

  /* A run holds a reference to us, so none can be running by now. */
  g_clear_object (&self->results);

  G_OBJECT_CLASS (palang_search_parent_class)->finalize (object);
}

static void
palang_search_class_init (PalangSearchClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = palang_search_get_property;
  object_class->finalize = palang_search_finalize;

  properties[PROP_RUNNING] =
    g_param_spec_boolean ("running", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[SIGNAL_FINISHED] =
    g_signal_new ("finished",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_STRING);
}

static void
palang_search_init (PalangSearch *self)
{
  self->results = g_list_store_new (PALANG_TYPE_SEARCH_MATCH);
}

PalangSearch *
palang_search_new (void)
{
  return g_object_new (PALANG_TYPE_SEARCH, NULL);
}
