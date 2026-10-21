/* palang-command-line.c — the `palang` subcommands (see the header).
 *
 * One table lists the subcommands; `help` is generated from it, and
 * `settings` from the settings table in palang-settings.c, so neither needs
 * updating when a command or a preference is added.
 *
 * The hidden `__complete` subcommand feeds the bash completion script
 * (data/palang.bash-completion) with names, so the script never needs
 * updating either.
 */

#include "config.h"

#include "palang-command-line.h"

/* A subcommand gets the words after its own name. */
typedef int (*SubcommandFunc) (PalangSettings     *settings,
                               const char * const *args,
                               GString            *output,
                               GString            *errors);

static int run_help     (PalangSettings *, const char * const *, GString *, GString *);
static int run_settings (PalangSettings *, const char * const *, GString *, GString *);
static int run_get      (PalangSettings *, const char * const *, GString *, GString *);
static int run_set      (PalangSettings *, const char * const *, GString *, GString *);
static int run_reset    (PalangSettings *, const char * const *, GString *, GString *);
static int run_complete (PalangSettings *, const char * const *, GString *, GString *);

static const struct {
  const char     *name;
  const char     *arguments;  /* for the help text */
  const char     *summary;    /* NULL: hidden from help and completion */
  SubcommandFunc  run;
} subcommands[] = {
  { "help",       "",           "Show this help", run_help },
  { "settings",   "",           "List every setting: value, allowed values, description", run_settings },
  { "get",        "NAME",       "Print the value of a setting", run_get },
  { "set",        "NAME VALUE", "Change a setting; it applies right away and is saved", run_set },
  { "reset",      "NAME",       "Set a setting back to its default", run_reset },
  { "__complete", "",           NULL, run_complete },
};

gboolean
palang_command_line_is_subcommand (const char *word)
{
  for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++)
    if (g_strcmp0 (subcommands[i].name, word) == 0)
      return TRUE;

  return FALSE;
}

int
palang_command_line_run (PalangSettings     *settings,
                         const char * const *args,
                         GString            *output,
                         GString            *errors)
{
  g_return_val_if_fail (PALANG_IS_SETTINGS (settings), 1);
  g_return_val_if_fail (args != NULL && args[0] != NULL, 1);

  for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++)
    if (g_str_equal (subcommands[i].name, args[0]))
      return subcommands[i].run (settings, args + 1, output, errors);

  g_string_append_printf (errors, "palang: unknown command “%s”. Try “palang help”.\n", args[0]);
  return 1;
}

/* Number of words in a NULL-terminated array. */
static guint
count_args (const char * const *args)
{
  return g_strv_length ((char **) args);
}

static int
usage_error (GString    *errors,
             const char *subcommand)
{
  for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++)
    if (g_str_equal (subcommands[i].name, subcommand))
      g_string_append_printf (errors, "Usage: palang %s %s\n", subcommand, subcommands[i].arguments);

  return 1;
}

/* Shows a settings error; for an unknown name, also where to look. */
static int
settings_error (GString      *errors,
                const GError *error)
{
  g_string_append_printf (errors, "palang: %s\n", error->message);
  if (g_error_matches (error, PALANG_SETTINGS_ERROR, PALANG_SETTINGS_ERROR_UNKNOWN_NAME))
    g_string_append (errors, "Run “palang settings” to see them all.\n");

  return 1;
}

/* Text values are shown in quotes when empty, so they're visible. */
static void
append_value (GString    *output,
              const char *value)
{
  if (value[0] == '\0')
    g_string_append (output, "\"\"");
  else
    g_string_append (output, value);
}

/* ---- Subcommands -------------------------------------------------------- */

static int
run_help (PalangSettings     *settings,
          const char * const *args,
          GString            *output,
          GString            *errors)
{
  g_auto (GStrv) names = palang_settings_dup_names ();
  g_autofree char *name_list = g_strjoinv (", ", names);

  g_string_append (output,
                   "Usage: palang COMMAND [ARGUMENTS]\n"
                   "       palang FILE…\n"
                   "\n"
                   "Commands (only in the Palang terminal):\n");

  for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++)
    {
      g_autofree char *call = NULL;

      if (subcommands[i].summary == NULL)
        continue;

      call = g_strdup_printf ("%s %s", subcommands[i].name, subcommands[i].arguments);
      /* %-18s pads the command to 18 characters, so the summaries line up. */
      g_string_append_printf (output, "  %-18s %s\n", call, subcommands[i].summary);
    }

  g_string_append_printf (output,
                          "\n"
                          "Settings: %s\n"
                          "\n"
                          "Anything else opens files. To open a file named like a command,\n"
                          "write it as a path: palang ./set\n",
                          name_list);
  return 0;
}

static int
run_settings (PalangSettings     *settings,
              const char * const *args,
              GString            *output,
              GString            *errors)
{
  g_auto (GStrv) names = palang_settings_dup_names ();

  if (count_args (args) != 0)
    return usage_error (errors, "settings");

  for (guint i = 0; names[i] != NULL; i++)
    {
      g_autofree char *value = palang_settings_dup_value (settings, names[i]);
      g_autofree char *allowed = palang_settings_dup_allowed_values (names[i]);
      g_autofree char *default_value = palang_settings_dup_default_value (names[i]);

      if (i > 0)
        g_string_append (output, "\n");

      g_string_append_printf (output, "%s = ", names[i]);
      append_value (output, value);
      g_string_append_printf (output, "\n    %s\n    Allowed: %s. Default: ",
                              palang_settings_get_description (names[i]), allowed);
      append_value (output, default_value);
      g_string_append (output, ".\n");
    }

  return 0;
}

static int
run_get (PalangSettings     *settings,
         const char * const *args,
         GString            *output,
         GString            *errors)
{
  g_autofree char *value = NULL;
  g_autoptr (GError) error = NULL;

  if (count_args (args) != 1)
    return usage_error (errors, "get");

  value = palang_settings_dup_value (settings, args[0]);
  if (value == NULL)
    {
      g_set_error (&error, PALANG_SETTINGS_ERROR, PALANG_SETTINGS_ERROR_UNKNOWN_NAME,
                   "There is no setting called “%s”.", args[0]);
      return settings_error (errors, error);
    }

  /* Printed as is (empty means empty), so scripts can use it. */
  g_string_append_printf (output, "%s\n", value);
  return 0;
}

static int
run_set (PalangSettings     *settings,
         const char * const *args,
         GString            *output,
         GString            *errors)
{
  g_autofree char *value = NULL;
  g_autofree char *new_value = NULL;
  g_autoptr (GError) error = NULL;

  if (count_args (args) < 2)
    return usage_error (errors, "set");

  /* The value may be several words: palang set font Monospace 12 */
  value = g_strjoinv (" ", (char **) args + 1);

  if (!palang_settings_set_from_string (settings, args[0], value, &error))
    return settings_error (errors, error);

  new_value = palang_settings_dup_value (settings, args[0]);
  g_string_append_printf (output, "%s = ", args[0]);
  append_value (output, new_value);
  g_string_append (output, "\n");
  return 0;
}

static int
run_reset (PalangSettings     *settings,
           const char * const *args,
           GString            *output,
           GString            *errors)
{
  g_autofree char *new_value = NULL;
  g_autoptr (GError) error = NULL;

  if (count_args (args) != 1)
    return usage_error (errors, "reset");

  if (!palang_settings_reset (settings, args[0], &error))
    return settings_error (errors, error);

  new_value = palang_settings_dup_value (settings, args[0]);
  g_string_append_printf (output, "%s = ", args[0]);
  append_value (output, new_value);
  g_string_append (output, " (the default)\n");
  return 0;
}

/* For the bash completion script, one word per line:
 *   palang __complete subcommands
 *   palang __complete names
 *   palang __complete values NAME   (nothing for free-form settings) */
static int
run_complete (PalangSettings     *settings,
              const char * const *args,
              GString            *output,
              GString            *errors)
{
  g_auto (GStrv) words = NULL;
  guint n_args = count_args (args);

  if (n_args == 1 && g_str_equal (args[0], "subcommands"))
    {
      for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++)
        if (subcommands[i].summary != NULL)
          g_string_append_printf (output, "%s\n", subcommands[i].name);
      return 0;
    }

  if (n_args == 1 && g_str_equal (args[0], "names"))
    words = palang_settings_dup_names ();
  else if (n_args == 2 && g_str_equal (args[0], "values"))
    words = palang_settings_dup_choices (args[1]);
  else
    return 1;

  for (guint i = 0; words != NULL && words[i] != NULL; i++)
    g_string_append_printf (output, "%s\n", words[i]);
  return 0;
}
