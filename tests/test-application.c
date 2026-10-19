/* test-application.c — smoke test for PalangApplication.
 *
 * Creating the application object doesn't need a display, so this runs
 * anywhere. Under AddressSanitizer it also checks that creating and freeing
 * the application doesn't leak or corrupt memory.
 */

#include "config.h"

#include "palang-application.h"

static void
test_application_id (void)
{
  g_autoptr (PalangApplication) app = palang_application_new ();

  g_assert_cmpstr (g_application_get_application_id (G_APPLICATION (app)),
                   ==, APPLICATION_ID);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/application/id", test_application_id);

  return g_test_run ();
}
