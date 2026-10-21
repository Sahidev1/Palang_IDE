/* test-palette.c — UI tests for PalangPalette: filtering, ordering and
 * picking an item. Up/Down need real key presses: MANUAL_TESTS.md. */

#include "config.h"

#include "palang-palette.h"

typedef struct {
  AdwWindow         *window;
  PalangPalette     *palette;  /* owned while open */
  GtkEditable       *entry;
  PalangPaletteItem *activated;
} Fixture;

static void
on_item_activated (PalangPalette     *palette,
                   PalangPaletteItem *item,
                   Fixture           *fixture)
{
  g_set_object (&fixture->activated, item);
}

static void
add_item (GListStore *items,
          const char *id,
          const char *title)
{
  g_autoptr (PalangPaletteItem) item = palang_palette_item_new (id, title, NULL, NULL);

  g_list_store_append (items, item);
}

static void
fixture_setup (Fixture *fixture, gconstpointer data)
{
  g_autoptr (GListStore) items = g_list_store_new (PALANG_TYPE_PALETTE_ITEM);
  GtkWidget *focus = NULL;

  add_item (items, "win.save-all", "Save All");
  add_item (items, "win.save", "Save");
  add_item (items, "win.toggle-panel", "Toggle Panel");
  add_item (items, "win.go-to-line", "Go to Line…");

  fixture->window = ADW_WINDOW (adw_window_new ());
  gtk_window_present (GTK_WINDOW (fixture->window));

  fixture->palette = g_object_ref_sink (palang_palette_new ("Run a command"));
  palang_palette_set_items (fixture->palette, G_LIST_MODEL (items));
  g_signal_connect (fixture->palette, "item-activated", G_CALLBACK (on_item_activated), fixture);
  adw_dialog_present (ADW_DIALOG (fixture->palette), GTK_WIDGET (fixture->window));

  /* The search box is the dialog's focus widget. */
  g_object_get (fixture->palette, "focus-widget", &focus, NULL);
  fixture->entry = GTK_EDITABLE (focus);
  g_object_unref (focus);  /* the dialog keeps it alive */
}

static void
fixture_teardown (Fixture *fixture, gconstpointer data)
{
  g_clear_object (&fixture->activated);
  g_clear_object (&fixture->palette);
  gtk_window_destroy (GTK_WINDOW (fixture->window));
}

static guint
n_matches (Fixture *fixture)
{
  return g_list_model_get_n_items (palang_palette_get_matches (fixture->palette));
}

static const char *
match_id (Fixture *fixture,
          guint    position)
{
  g_autoptr (PalangPaletteItem) item = g_list_model_get_item (palang_palette_get_matches (fixture->palette),
                                                              position);

  /* The list keeps the item alive, so the id stays valid. */
  return palang_palette_item_get_id (item);
}

/* Types @text and waits until the list shows @expected items. */
static void
type_and_wait (Fixture    *fixture,
               const char *text,
               guint       expected)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  gtk_editable_set_text (fixture->entry, text);
  while (n_matches (fixture) != expected)
    {
      if (g_get_monotonic_time () > deadline)
        g_error ("Timed out: %u matches, expected %u", n_matches (fixture), expected);
      g_main_context_iteration (NULL, FALSE);
    }
}

static void
test_shows_all_in_order (Fixture *fixture, gconstpointer data)
{
  g_assert_cmpuint (n_matches (fixture), ==, 4);
  g_assert_cmpstr (match_id (fixture, 0), ==, "win.save-all");
  g_assert_cmpstr (match_id (fixture, 3), ==, "win.go-to-line");
}

static void
test_filters_and_sorts (Fixture *fixture, gconstpointer data)
{
  type_and_wait (fixture, "sva", 1);
  g_assert_cmpstr (match_id (fixture, 0), ==, "win.save-all");

  /* Both match "save" equally well; the shorter one comes first. */
  type_and_wait (fixture, "save", 2);
  g_assert_cmpstr (match_id (fixture, 0), ==, "win.save");

  type_and_wait (fixture, "xyz", 0);
  type_and_wait (fixture, "", 4);
}

/* Enter picks the best match and closes the palette. */
static void
test_enter_activates (Fixture *fixture, gconstpointer data)
{
  type_and_wait (fixture, "tgp", 1);
  g_signal_emit_by_name (fixture->entry, "activate");

  g_assert_nonnull (fixture->activated);
  g_assert_cmpstr (palang_palette_item_get_id (fixture->activated), ==, "win.toggle-panel");
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  adw_init ();
  g_object_set (gtk_settings_get_default (), "gtk-enable-animations", FALSE, NULL);

#define ADD_TEST(path, func) \
  g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD_TEST ("/palette/shows-all-in-order", test_shows_all_in_order);
  ADD_TEST ("/palette/filters-and-sorts", test_filters_and_sorts);
  ADD_TEST ("/palette/enter-activates", test_enter_activates);

#undef ADD_TEST

  return g_test_run ();
}
