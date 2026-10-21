/* palang-command.c — one entry of the command palette (see the header).
 *
 * A small value object: three strings set once at creation and never
 * changed, exposed as read-only properties so list widgets can show them.
 */

#include "config.h"

#include "palang-command.h"

struct _PalangCommand
{
  GObject parent_instance;

  char *action_name;
  char *title;
  char *accel;  /* nullable */
};

G_DEFINE_FINAL_TYPE (PalangCommand, palang_command, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_ACTION_NAME,
  PROP_TITLE,
  PROP_ACCEL,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

const char *
palang_command_get_action_name (PalangCommand *self)
{
  g_return_val_if_fail (PALANG_IS_COMMAND (self), NULL);

  return self->action_name;
}

const char *
palang_command_get_title (PalangCommand *self)
{
  g_return_val_if_fail (PALANG_IS_COMMAND (self), NULL);

  return self->title;
}

const char *
palang_command_get_accel (PalangCommand *self)
{
  g_return_val_if_fail (PALANG_IS_COMMAND (self), NULL);

  return self->accel;
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_command_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PalangCommand *self = PALANG_COMMAND (object);

  switch (prop_id)
    {
    case PROP_ACTION_NAME:
      g_value_set_string (value, self->action_name);
      break;

    case PROP_TITLE:
      g_value_set_string (value, self->title);
      break;

    case PROP_ACCEL:
      g_value_set_string (value, self->accel);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* Only called while the object is being created: the properties are
 * CONSTRUCT_ONLY, so they can't change afterwards. */
static void
palang_command_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  PalangCommand *self = PALANG_COMMAND (object);

  switch (prop_id)
    {
    case PROP_ACTION_NAME:
      self->action_name = g_value_dup_string (value);
      break;

    case PROP_TITLE:
      self->title = g_value_dup_string (value);
      break;

    case PROP_ACCEL:
      self->accel = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
palang_command_finalize (GObject *object)
{
  PalangCommand *self = PALANG_COMMAND (object);

  g_free (self->action_name);
  g_free (self->title);
  g_free (self->accel);

  G_OBJECT_CLASS (palang_command_parent_class)->finalize (object);
}

static void
palang_command_class_init (PalangCommandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = palang_command_get_property;
  object_class->set_property = palang_command_set_property;
  object_class->finalize = palang_command_finalize;

  properties[PROP_ACTION_NAME] =
    g_param_spec_string ("action-name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  properties[PROP_ACCEL] =
    g_param_spec_string ("accel", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
palang_command_init (PalangCommand *self)
{
}

PalangCommand *
palang_command_new (const char *action_name,
                    const char *title,
                    const char *accel)
{
  g_return_val_if_fail (action_name != NULL, NULL);
  g_return_val_if_fail (title != NULL, NULL);

  return g_object_new (PALANG_TYPE_COMMAND,
                       "action-name", action_name,
                       "title", title,
                       "accel", accel,
                       NULL);
}
