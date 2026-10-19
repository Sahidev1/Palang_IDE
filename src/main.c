/* main.c — program entry point.
 *
 * All real work happens in PalangApplication (src/app/palang-application.c).
 * g_application_run() starts the GLib main loop and only returns when the
 * application quits.
 */

#include "palang-application.h"

int
main (int   argc,
      char *argv[])
{
  /* g_autoptr: the application is unreffed (freed) automatically when
   * `app` goes out of scope at the end of main(). */
  g_autoptr (PalangApplication) app = palang_application_new ();

  return g_application_run (G_APPLICATION (app), argc, argv);
}
