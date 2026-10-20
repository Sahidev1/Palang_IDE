/* palang-settings.c — preferences as properties, stored in a GKeyFile.
 *
 * The GKeyFile holds the whole file as loaded, including groups we don't
 * know about. Setting a property updates the matching key and saves the
 * file, so unknown content survives.
 *
 * To add a preference: add it to the `options` table below (with its
 * group, key, type and default), add a PROP_ enum value in the same order,
 * and give it a getter if C code needs one. get/set_property and loading
 * are driven by the table.
 */

#include "config.h"

#include "palang-settings.h"

typedef enum {
  OPTION_STRING,
  OPTION_BOOLEAN,
  OPTION_UINT,
} OptionType;

typedef struct {
  const char *name;     /* property name, also the key in the file */
  const char *group;    /* [group] in the file */
  OptionType  type;
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
  { "color-scheme", "appearance", OPTION_STRING, .default_string = "dark",
    .choices = color_scheme_choices },
  { "font", "editor", OPTION_STRING, .default_string = "" },
  { "tab-width", "editor", OPTION_UINT, .default_uint = 4, .min_uint = 1, .max_uint = 16 },
  { "insert-spaces", "editor", OPTION_BOOLEAN, .default_boolean = TRUE },
  { "show-line-numbers", "editor", OPTION_BOOLEAN, .default_boolean = TRUE },
  { "highlight-current-line", "editor", OPTION_BOOLEAN, .default_boolean = TRUE },
  { "wrap-lines", "editor", OPTION_BOOLEAN, .default_boolean = FALSE },
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
