/* palang-preferences-dialog.c — connect each preferences row to a setting.
 *
 * Most rows are simple two-way property bindings (g_object_bind_property
 * with G_BINDING_BIDIRECTIONAL): the switch shows the setting, and flipping
 * the switch changes it. Two need converting values:
 *   - Style: the combo row's selected index ↔ "system" / "light" / "dark"
 *   - Font: the font button's PangoFontDescription ↔ the font string
 */

#include "config.h"

#include "palang-preferences-dialog.h"

/* Combo row positions, in the order of the .ui file's string list. */
static const char *const color_schemes[] = { "system", "light", "dark" };

/* Font used when the user turns off "Use System Monospace Font". */
#define DEFAULT_CUSTOM_FONT "Monospace 11"

struct _PalangPreferencesDialog
{
  AdwPreferencesDialog parent_instance;

  PalangSettings *settings;  /* owned */

  /* Template children */
  AdwComboRow        *color_scheme_row;
  AdwSwitchRow       *system_font_row;
  AdwActionRow       *font_row;
  GtkFontDialogButton *font_button;
  AdwSwitchRow       *line_numbers_row;
  AdwSwitchRow       *current_line_row;
  AdwSwitchRow       *wrap_lines_row;
  AdwSpinRow         *tab_width_row;
  AdwSwitchRow       *insert_spaces_row;
};

G_DEFINE_FINAL_TYPE (PalangPreferencesDialog, palang_preferences_dialog,
                     ADW_TYPE_PREFERENCES_DIALOG)

/* ---- Style: setting string ↔ combo position ----------------------------- */

static gboolean
color_scheme_to_position (GBinding     *binding,
                          const GValue *from_value,
                          GValue       *to_value,
                          gpointer      user_data)
{
  const char *scheme = g_value_get_string (from_value);

  for (guint i = 0; i < G_N_ELEMENTS (color_schemes); i++)
    if (g_strcmp0 (scheme, color_schemes[i]) == 0)
      {
        g_value_set_uint (to_value, i);
        return TRUE;
      }

  return FALSE;
}

static gboolean
position_to_color_scheme (GBinding     *binding,
                          const GValue *from_value,
                          GValue       *to_value,
                          gpointer      user_data)
{
  guint position = g_value_get_uint (from_value);

  if (position >= G_N_ELEMENTS (color_schemes))
    return FALSE;

  g_value_set_string (to_value, color_schemes[position]);
  return TRUE;
}

/* ---- Font: setting string ↔ font button --------------------------------- */

static void
update_font_rows (PalangPreferencesDialog *self)
{
  const char *font = palang_settings_get_font (self->settings);
  gboolean system_font = font[0] == '\0';

  g_autoptr (PangoFontDescription) description = NULL;

  adw_switch_row_set_active (self->system_font_row, system_font);
  gtk_widget_set_sensitive (GTK_WIDGET (self->font_row), !system_font);

  /* While the system font is used, the (greyed out) button shows the font
   * that will be used if the user turns the switch off. */
  description = pango_font_description_from_string (system_font ? DEFAULT_CUSTOM_FONT : font);
  gtk_font_dialog_button_set_font_desc (self->font_button, description);
}

static void
on_font_setting_changed (PalangSettings          *settings,
                         GParamSpec              *pspec,
                         PalangPreferencesDialog *self)
{
  update_font_rows (self);
}

/* The user picked a font in the font chooser. */
static void
on_font_button_changed (GtkFontDialogButton     *button,
                        GParamSpec              *pspec,
                        PalangPreferencesDialog *self)
{
  PangoFontDescription *description = gtk_font_dialog_button_get_font_desc (button);
  g_autofree char *font = NULL;

  if (description == NULL || adw_switch_row_get_active (self->system_font_row))
    return;

  font = pango_font_description_to_string (description);
  g_object_set (self->settings, "font", font, NULL);
}

/* The user flipped "Use System Monospace Font". */
static void
on_system_font_toggled (AdwSwitchRow            *row,
                        GParamSpec              *pspec,
                        PalangPreferencesDialog *self)
{
  gboolean system_font = adw_switch_row_get_active (row);
  const char *current = palang_settings_get_font (self->settings);

  if (system_font)
    g_object_set (self->settings, "font", "", NULL);
  else if (current[0] == '\0')
    g_object_set (self->settings, "font", DEFAULT_CUSTOM_FONT, NULL);
}

/* ---- GObject boilerplate ------------------------------------------------ */

static void
palang_preferences_dialog_dispose (GObject *object)
{
  PalangPreferencesDialog *self = PALANG_PREFERENCES_DIALOG (object);

  if (self->settings != NULL)
    g_signal_handlers_disconnect_by_data (self->settings, self);
  g_clear_object (&self->settings);

  gtk_widget_dispose_template (GTK_WIDGET (self), PALANG_TYPE_PREFERENCES_DIALOG);

  G_OBJECT_CLASS (palang_preferences_dialog_parent_class)->dispose (object);
}

static void
palang_preferences_dialog_class_init (PalangPreferencesDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = palang_preferences_dialog_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               RESOURCE_PATH "/ui/palang-preferences-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, color_scheme_row);
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, system_font_row);
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, font_row);
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, font_button);
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, line_numbers_row);
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, current_line_row);
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, wrap_lines_row);
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, tab_width_row);
  gtk_widget_class_bind_template_child (widget_class, PalangPreferencesDialog, insert_spaces_row);
}

static void
palang_preferences_dialog_init (PalangPreferencesDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

AdwDialog *
palang_preferences_dialog_new (PalangSettings *settings)
{
  PalangPreferencesDialog *self;
  const GBindingFlags both_ways = G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE;

  g_return_val_if_fail (PALANG_IS_SETTINGS (settings), NULL);

  self = g_object_new (PALANG_TYPE_PREFERENCES_DIALOG, NULL);
  self->settings = g_object_ref (settings);

  g_object_bind_property_full (settings, "color-scheme", self->color_scheme_row, "selected",
                               both_ways,
                               color_scheme_to_position, position_to_color_scheme,
                               NULL, NULL);
  g_object_bind_property (settings, "show-line-numbers", self->line_numbers_row, "active", both_ways);
  g_object_bind_property (settings, "highlight-current-line", self->current_line_row, "active", both_ways);
  g_object_bind_property (settings, "wrap-lines", self->wrap_lines_row, "active", both_ways);
  /* GBinding converts between the setting's uint and the spin row's double. */
  g_object_bind_property (settings, "tab-width", self->tab_width_row, "value", both_ways);
  g_object_bind_property (settings, "insert-spaces", self->insert_spaces_row, "active", both_ways);

  update_font_rows (self);
  g_signal_connect (settings, "notify::font", G_CALLBACK (on_font_setting_changed), self);
  g_signal_connect (self->font_button, "notify::font-desc", G_CALLBACK (on_font_button_changed), self);
  g_signal_connect (self->system_font_row, "notify::active", G_CALLBACK (on_system_font_toggled), self);

  return ADW_DIALOG (self);
}
