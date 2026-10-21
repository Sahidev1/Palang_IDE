/* palang-settings.c — preferences as properties, stored in a GKeyFile.
 *
 * The GKeyFile holds the whole file as loaded, including groups we don't
 * know about. Setting a property updates the matching key and saves the
 * file, so unknown content survives.
 *
 * To add a preference: add it to the `options` table below (with its
 * group, key, type, default and a description), add a PROP_ enum value in
 * the same order, and give it a getter if C code needs one.
 * get/set_property, loading, and reading/writing settings by name as text
 * (the `palang` command in the terminal) are driven by the table, so a new
 * preference works there without further code.
 */

#include "config.h"

#include "palang-settings.h"

G_DEFINE_QUARK (palang-settings-error-quark, palang_settings_error)

typedef enum {
  OPTION_STRING,
  OPTION_BOOLEAN,
  OPTION_UINT,
} OptionType;

typedef struct {
  const char *name;     /* property name, also the key in the file */
  const char *group;    /* [group] in the file */
  OptionType  type;
  const char *description;  /* one sentence, shown by `palang settings` */
  const char *default_string;
  gboolean    default_boolean;
  guint       default_uint;
  guint       min_uint;
  guint       max_uint;
  /* Allowed values for a string option, NULL-terminated; NULL = anything. */
  const char *const *choices;
} Option;

static const char *const color_scheme_choices[] = { "system", "light", "dark", NULL };

/* Order must match the PROP_ enum below. */
static const Option options[] = {
  /* Dark, not "system": following the system doesn't work reliably on MATE
   * yet (see PROGRESS.md, Phase 1 manual test results). */
  { "color-scheme", "appearance", OPTION_STRING,
    "Light or dark style for the whole window.",
    .default_string = "dark", .choices = color_scheme_choices },
  { "font", "editor", OPTION_STRING,
    "Font of the editor and the terminal, like “Monospace 11”. Empty: the system's monospace font.",
    .default_string = "" },
  { "tab-width", "editor", OPTION_UINT,
    "Width of a tab, in spaces.",
    .default_uint = 4, .min_uint = 1, .max_uint = 16 },
  { "insert-spaces", "editor", OPTION_BOOLEAN,
    "Insert spaces instead of a tab character when Tab is pressed.",
    .default_boolean = TRUE },
  { "show-line-numbers", "editor", OPTION_BOOLEAN,
    "Show line numbers next to the text.",
    .default_boolean = TRUE },
  { "highlight-current-line", "editor", OPTION_BOOLEAN,
    "Highlight the line the cursor is on.",
    .default_boolean = TRUE },
  { "wrap-lines", "editor", OPTION_BOOLEAN,
    "Wrap long lines at the edge of the editor instead of scrolling sideways.",
    .default_boolean = FALSE },
  /* Language servers (lsp/). The value is a command line, split like a
   * shell would ("typescript-language-server --stdio"). The names end in
   * "-server" after the keys of the server table in palang-lsp-languages.c. */
  { "c-server", "lsp", OPTION_STRING,
    "Command that starts the language server for C and C++. Empty: none.",
    .default_string = "clangd" },
  { "python-server", "lsp", OPTION_STRING,
    "Command that starts the language server for Python. Empty: none.",
    .default_string = "pylsp" },
  { "javascript-server", "lsp", OPTION_STRING,
    "Command that starts the language server for JavaScript and TypeScript. Empty: none.",
    .default_string = "typescript-language-server --stdio" },
};

enum {
  PROP_0,
  PROP_COLOR_SCHEME,
  PROP_FONT,
  PROP_TAB_WIDTH,
  PROP_INSERT_SPACES,
  PROP_SHOW_LINE_NUMBERS,
  PROP_HIGHLIGHT_CURRENT_LINE,
  PROP_WRAP_LINES,
  PROP_C_SERVER,
  PROP_PYTHON_SERVER,
  PROP_JAVASCRIPT_SERVER,
  N_PROPS
};

G_STATIC_ASSERT (G_N_ELEMENTS (options) == N_PROPS - 1);

struct _PalangSettings
{
  GObject parent_instance;

  char     *path;      /* owned */
  GKeyFile *key_file;  /* owned; the whole file */

  /* Current values, indexed by PROP_ id - 1. */
  char     *strings[N_PROPS - 1];
  gboolean  booleans[N_PROPS - 1];
  guint     uints[N_PROPS - 1];
};

G_DEFINE_FINAL_TYPE (PalangSettings, palang_settings, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

/* ---- Loading and saving ------------------------------------------------- */

static gboolean
is_valid_string (const Option *option,
                 const char   *value)
{
  if (option->choices == NULL)
    return TRUE;

  return g_strv_contains (option->choices, value);
}

/* Reads every option from the key file, using defaults for missing or
 * invalid values. */
static void
read_options (PalangSettings *self)
{
  for (guint i = 0; i < G_N_ELEMENTS (options); i++)
    {
      const Option *option = &options[i];
      g_autoptr (GError) error = NULL;

      switch (option->type)
        {
        case OPTION_STRING:
          {
            g_autofree char *value = g_key_file_get_string (self->key_file, option->group,
                                                            option->name, &error);

            g_free (self->strings[i]);
            if (value != NULL && is_valid_string (option, value))
              self->strings[i] = g_steal_pointer (&value);
            else
              self->strings[i] = g_strdup (option->default_string);
          }
          break;

        case OPTION_BOOLEAN:
          self->booleans[i] = g_key_file_get_boolean (self->key_file, option->group,
                                                      option->name, &error);
          if (error != NULL)
            self->booleans[i] = option->default_boolean;
          break;

        case OPTION_UINT:
          {
            int value = g_key_file_get_integer (self->key_file, option->group,
                                                option->name, &error);

            if (error != NULL || value < (int) option->min_uint || value > (int) option->max_uint)
              self->uints[i] = option->default_uint;
            else
              self->uints[i] = (guint) value;
          }
          break;

        default:
          g_assert_not_reached ();
        }
    }
}

static void
save (PalangSettings *self)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *directory = g_path_get_dirname (self->path);

  g_mkdir_with_parents (directory, 0700);

  /* g_key_file_save_to_file writes a temporary file and renames it, so a
   * crash can't leave a half-written config.ini. */
  if (!g_key_file_save_to_file (self->key_file, self->path, &error))
    g_warning ("Could not save settings to %s: %s", self->path, error->message);
}

/* ---- Properties --------------------------------------------------------- */

static void
palang_settings_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PalangSettings *self = PALANG_SETTINGS (object);
  guint i = prop_id - 1;

  if (prop_id == 0 || prop_id >= N_PROPS)
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
    }

  switch (options[i].type)
    {
    case OPTION_STRING:
      g_value_set_string (value, self->strings[i]);
      break;

    case OPTION_BOOLEAN:
      g_value_set_boolean (value, self->booleans[i]);
      break;

    case OPTION_UINT:
      g_value_set_uint (value, self->uints[i]);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
palang_settings_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PalangSettings *self = PALANG_SETTINGS (object);
  guint i = prop_id - 1;
  const Option *option;

  if (prop_id == 0 || prop_id >= N_PROPS)
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
    }

  option = &options[i];

  switch (option->type)
    {
    case OPTION_STRING:
      {
        const char *new_value = g_value_get_string (value);

        if (new_value == NULL)
          new_value = "";
        if (!is_valid_string (option, new_value))
          {
            g_warning ("Invalid value “%s” for setting %s", new_value, option->name);
            return;
          }
        if (g_strcmp0 (self->strings[i], new_value) == 0)
          return;

        g_free (self->strings[i]);
        self->strings[i] = g_strdup (new_value);
        g_key_file_set_string (self->key_file, option->group, option->name, new_value);
      }
      break;

    case OPTION_BOOLEAN:
      if (self->booleans[i] == g_value_get_boolean (value))
        return;

      self->booleans[i] = g_value_get_boolean (value);
      g_key_file_set_boolean (self->key_file, option->group, option->name, self->booleans[i]);
      break;

    case OPTION_UINT:
      /* The GParamSpec's min/max already keep the value in range. */
      if (self->uints[i] == g_value_get_uint (value))
        return;

      self->uints[i] = g_value_get_uint (value);
      g_key_file_set_integer (self->key_file, option->group, option->name, (int) self->uints[i]);
      break;

    default:
      g_assert_not_reached ();
    }

  save (self);
  g_object_notify_by_pspec (object, pspec);
}

const char *
palang_settings_get_color_scheme (PalangSettings *self)
{
  g_return_val_if_fail (PALANG_IS_SETTINGS (self), NULL);

  return self->strings[PROP_COLOR_SCHEME - 1];
}

const char *
palang_settings_get_font (PalangSettings *self)
{
  g_return_val_if_fail (PALANG_IS_SETTINGS (self), NULL);

  return self->strings[PROP_FONT - 1];
}

guint
palang_settings_get_tab_width (PalangSettings *self)
{
  g_return_val_if_fail (PALANG_IS_SETTINGS (self), 0);

  return self->uints[PROP_TAB_WIDTH - 1];
}

/* ---- Settings by name, as text ------------------------------------------
 *
 * For the `palang` command in the terminal, which deals in names and text
 * ("tab-width", "2"). Values are checked the same way as everywhere else,
 * and setting one goes through the property, so it is saved and applied
 * live exactly like a change in Preferences. */

/* Returns: (nullable): the table row for @name. */
static const Option *
find_option (const char *name)
{
  for (guint i = 0; i < G_N_ELEMENTS (options); i++)
    if (g_strcmp0 (options[i].name, name) == 0)
      return &options[i];

  return NULL;
}

static gboolean
check_name (const char  *name,
            GError     **error)
{
  if (find_option (name) != NULL)
    return TRUE;

  g_set_error (error, PALANG_SETTINGS_ERROR, PALANG_SETTINGS_ERROR_UNKNOWN_NAME,
               "There is no setting called “%s”.", name);
  return FALSE;
}

/* Returns: (transfer full): @value of @option written as text. */
static char *
format_value (const Option *option,
              const char   *string_value,
              gboolean      boolean_value,
              guint         uint_value)
{
  switch (option->type)
    {
    case OPTION_STRING:
      return g_strdup (string_value);

    case OPTION_BOOLEAN:
      return g_strdup (boolean_value ? "true" : "false");

    case OPTION_UINT:
      return g_strdup_printf ("%u", uint_value);

    default:
      g_assert_not_reached ();
    }
}

char **
palang_settings_dup_names (void)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

  for (guint i = 0; i < G_N_ELEMENTS (options); i++)
    g_strv_builder_add (builder, options[i].name);

  return g_strv_builder_end (builder);
}

const char *
palang_settings_get_description (const char *name)
{
  const Option *option = find_option (name);

  return option != NULL ? option->description : NULL;
}

char **
palang_settings_dup_choices (const char *name)
{
  const Option *option = find_option (name);
  const char *booleans[] = { "true", "false", NULL };

  if (option == NULL)
    return NULL;

  if (option->type == OPTION_BOOLEAN)
    return g_strdupv ((char **) booleans);

  if (option->type == OPTION_STRING && option->choices != NULL)
    return g_strdupv ((char **) option->choices);

  return NULL;
}

char *
palang_settings_dup_allowed_values (const char *name)
{
  const Option *option = find_option (name);
  g_auto (GStrv) choices = palang_settings_dup_choices (name);

  if (option == NULL)
    return NULL;

  if (choices != NULL)
    return g_strjoinv (", ", choices);

  if (option->type == OPTION_UINT)
    return g_strdup_printf ("a whole number from %u to %u", option->min_uint, option->max_uint);

  return g_strdup ("any text");
}

char *
palang_settings_dup_default_value (const char *name)
{
  const Option *option = find_option (name);

  if (option == NULL)
    return NULL;

  return format_value (option, option->default_string, option->default_boolean,
                       option->default_uint);
}

char *
palang_settings_dup_value (PalangSettings *self,
                           const char     *name)
{
  const Option *option = find_option (name);
  guint i;

  g_return_val_if_fail (PALANG_IS_SETTINGS (self), NULL);

  if (option == NULL)
    return NULL;

  i = option - options;  /* the row's index in the table */
  return format_value (option, self->strings[i], self->booleans[i], self->uints[i]);
}

gboolean
palang_settings_set_from_string (PalangSettings  *self,
                                 const char      *name,
                                 const char      *value,
                                 GError         **error)
{
  const Option *option = find_option (name);
  g_autofree char *allowed = NULL;

  g_return_val_if_fail (PALANG_IS_SETTINGS (self), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (!check_name (name, error))
    return FALSE;

  allowed = palang_settings_dup_allowed_values (name);

  /* g_object_set () goes through set_property, which saves the file and
   * notifies everyone bound to the property. */
  switch (option->type)
    {
    case OPTION_STRING:
      if (!is_valid_string (option, value))
        break;
      g_object_set (self, name, value, NULL);
      return TRUE;

    case OPTION_BOOLEAN:
      if (g_strcmp0 (value, "true") == 0 || g_strcmp0 (value, "false") == 0)
        {
          g_object_set (self, name, g_str_equal (value, "true"), NULL);
          return TRUE;
        }
      break;

    case OPTION_UINT:
      {
        guint64 number;

        /* Checks that it's all digits and within the range in one go. */
        if (g_ascii_string_to_unsigned (value, 10, option->min_uint, option->max_uint,
                                        &number, NULL))
          {
            g_object_set (self, name, (guint) number, NULL);
            return TRUE;
          }
      }
      break;

    default:
      g_assert_not_reached ();
    }

  g_set_error (error, PALANG_SETTINGS_ERROR, PALANG_SETTINGS_ERROR_INVALID_VALUE,
               "“%s” is not a valid value for %s. Allowed: %s.", value, name, allowed);
  return FALSE;
}

gboolean
palang_settings_reset (PalangSettings  *self,
                       const char      *name,
                       GError         **error)
{
  g_autofree char *default_value = NULL;

  g_return_val_if_fail (PALANG_IS_SETTINGS (self), FALSE);

  if (!check_name (name, error))
    return FALSE;

  default_value = palang_settings_dup_default_value (name);
  return palang_settings_set_from_string (self, name, default_value, error);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_settings_finalize (GObject *object)
{
  PalangSettings *self = PALANG_SETTINGS (object);

  for (guint i = 0; i < G_N_ELEMENTS (options); i++)
    g_free (self->strings[i]);
  g_clear_pointer (&self->key_file, g_key_file_unref);
  g_free (self->path);

  G_OBJECT_CLASS (palang_settings_parent_class)->finalize (object);
}

static void
palang_settings_class_init (PalangSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = palang_settings_get_property;
  object_class->set_property = palang_settings_set_property;
  object_class->finalize = palang_settings_finalize;

  /* One property per entry in the options table. EXPLICIT_NOTIFY: we only
   * notify when a value really changes. */
  for (guint i = 0; i < G_N_ELEMENTS (options); i++)
    {
      const Option *option = &options[i];
      GParamFlags flags = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

      switch (option->type)
        {
        case OPTION_STRING:
          properties[i + 1] = g_param_spec_string (option->name, NULL, NULL,
                                                   option->default_string, flags);
          break;

        case OPTION_BOOLEAN:
          properties[i + 1] = g_param_spec_boolean (option->name, NULL, NULL,
                                                    option->default_boolean, flags);
          break;

        case OPTION_UINT:
          properties[i + 1] = g_param_spec_uint (option->name, NULL, NULL,
                                                 option->min_uint, option->max_uint,
                                                 option->default_uint, flags);
          break;

        default:
          g_assert_not_reached ();
        }
    }

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
palang_settings_init (PalangSettings *self)
{
  self->key_file = g_key_file_new ();
}

PalangSettings *
palang_settings_new_for_path (const char *path)
{
  PalangSettings *self;
  g_autoptr (GError) error = NULL;

  g_return_val_if_fail (path != NULL, NULL);

  self = g_object_new (PALANG_TYPE_SETTINGS, NULL);
  self->path = g_strdup (path);

  /* KEEP_COMMENTS so comments the user wrote survive our rewrites. A
   * missing file is normal (first start); other errors are reported. */
  if (!g_key_file_load_from_file (self->key_file, path, G_KEY_FILE_KEEP_COMMENTS, &error) &&
      !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
    g_warning ("Could not read settings from %s: %s", path, error->message);

  read_options (self);
  return self;
}

PalangSettings *
palang_settings_get_default (void)
{
  static PalangSettings *default_settings;

  if (default_settings == NULL)
    {
      g_autofree char *path = g_build_filename (g_get_user_config_dir (),
                                                "palang", "config.ini", NULL);
      default_settings = palang_settings_new_for_path (path);
    }

  return default_settings;
}
