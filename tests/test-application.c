/* test-application.c — smoke test for PalangApplication.
 *
 * Creating the application object doesn't need a display, so this runs
 * anywhere. Under AddressSanitizer it also checks that creating and freeing
 * the application doesn't leak or corrupt memory.
 */

#include "config.h"

#include "palang-application.h"
#include "palang-command.h"

static void
test_application_id (void)
{
  g_autoptr (PalangApplication) app = palang_application_new ();

  g_assert_cmpstr (g_application_get_application_id (G_APPLICATION (app)),
                   ==, APPLICATION_ID);
}

/* The command table: every command has a name for people and an action,
 * and no two commands share a shortcut. */
static void
test_commands (void)
{
  g_autoptr (PalangApplication) app = palang_application_new ();
  GListModel *commands = palang_application_get_commands (app);
  g_autoptr (GHashTable) accels = g_hash_table_new (g_str_hash, g_str_equal);
  guint n = g_list_model_get_n_items (commands);
  gboolean found_save_all = FALSE;

  g_assert_cmpuint (n, >, 10);
  g_assert_true (g_list_model_get_item_type (commands) == PALANG_TYPE_COMMAND);

  for (guint i = 0; i < n; i++)
    {
      g_autoptr (PalangCommand) command = g_list_model_get_item (commands, i);
      const char *action = palang_command_get_action_name (command);
      const char *accel = palang_command_get_accel (command);

      g_assert_true (g_str_has_prefix (action, "app.") || g_str_has_prefix (action, "win."));
      g_assert_cmpstr (palang_command_get_title (command), !=, "");

      if (accel != NULL)
        {
          g_assert_false (g_hash_table_contains (accels, accel));
          g_hash_table_add (accels, (gpointer) accel);
        }

      if (g_str_equal (action, "win.save-all"))
        {
          found_save_all = TRUE;
          g_assert_cmpstr (palang_command_get_title (command), ==, "Save All");
          g_assert_cmpstr (accel, ==, "<Control><Alt>s");
        }
    }

  g_assert_true (found_save_all);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/application/id", test_application_id);
  g_test_add_func ("/application/commands", test_commands);

  return g_test_run ();
}
