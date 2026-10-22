# How Palang's look works

A guide to the interim round between Phase 2 and Phase 3 (2026-10-22):
the logo, the icons, the colors and the window frame. Nearly all of it is
data (`data/`), not code: CSS, SVG icons and an editor color scheme.
Where code was needed, it's small and noted below.

## The big picture

```
data/icons/io.github.sahidev1.Palang.svg   app icon (installed for the menu)
data/icons/scalable/actions/palang-*.svg   icons inside the app (gresource)
data/style.css                             Palang's CSS, both modes
data/style-dark.css                        added on top in dark mode
data/styles/palang-dark.xml                the editor's dark color scheme
design/logo/, design/icons/                sources, drafts, comparison sheets
```

`data/palang.gresource.xml` bundles everything except the app icon into
the binary. Two things find it there **by themselves**, through the
app's resource base path (`/io/github/sahidev1/Palang`):

- **AdwApplication** loads `style.css`, and `style-dark.css` whenever the
  dark style is on (also `style-hc.css` / `style-hc-dark.css` for high
  contrast, which we don't have).
- **GtkIconTheme** searches `<base path>/icons/`, so a widget names an
  icon (`palang-close-symbolic`) and GTK finds our file.

## Why our CSS beats libadwaita's

GTK applies CSS from several *style providers*, each with a priority.
libadwaita's theme has a low one (THEME), the app's `style.css` a high
one (APPLICATION). **The higher priority wins, whatever the selectors
are.** That's what makes the short rules work:

```css
* { border-radius: 0; }      /* overrides every rounded corner */
```

Inside one file, the usual CSS rule applies (the more specific selector
wins), which is why `radio { border-radius: 9999px; }` beats `*`.

## Colors: named colors

libadwaita draws with **named colors** (`@define-color name value;`):
`accent_bg_color`, `window_bg_color`, `headerbar_bg_color`, `borders`, …
An app that defines the same name replaces libadwaita's value
everywhere it is used. Palang does this for:

| Name | Light (`style.css`) | Dark (`style-dark.css`) |
|---|---|---|
| `accent_bg_color` (switch fill, selection) | `#c37d0d` | `#f5b041` (logo amber) |
| `accent_fg_color` (text on that fill) | white | dark |
| `accent_color` (amber text and lines) | `#9a5b00` | `#f5b041` |
| `view_bg_color`, `window_bg_color`, `sidebar_bg_color`, `headerbar_bg_color`, `dialog_bg_color`, `borders` | libadwaita's | darker, each area a step apart |

The full list of libadwaita's names is in its stylesheet:
`gresource extract /usr/lib/x86_64-linux-gnu/libadwaita-1.so.0 /org/gnome/Adwaita/styles/defaults-dark.css`
(`base.css` has the rules that use them). That's also how the window
frame was copied (next section).

**Rule for later phases:** don't use libadwaita's `accent` class to mean
"information": it's amber now, like a warning. Use `palang-info` (blue).

## The window frame

Without a compositor (e.g. on Xvfb), libadwaita draws windows with a
solid frame (`window.solid-csd`): a thin line, a band in the header bar's
color, a second thin line. sahi liked it, so `style.css` draws the same
frame on the main window (`window.palang-window.csd`) with libadwaita's
drop shadow in front, square corners, and no frame when maximized or
tiled. The three `inset` box shadows are the frame. The class
`palang-window` is set in `data/ui/palang-window.ui`.

## Icons

Symbolic icons (names ending in `-symbolic`) are recolored by GTK:

- every shape gets the text color;
- a shape with `class="warning"`, `"error"` or `"success"` gets that
  color of the *icon palette*. Our icons draw their amber detail with
  `class="warning"`, and `style.css` sets the palette's warning color to
  amber (`-gtk-icon-palette: warning …`), so the accent follows the mode.

How to draw one: `data/icons/README.md`. To add one: the SVG in
`data/icons/scalable/actions/`, a line in `palang.gresource.xml`, and
use its name. To check a change: `design/icons/render-sheet.sh`.

**The file tree** chooses icons in code (`icon_name_for ()` in
`src/workspace/palang-file-tree.c`): folders by whether they are
expanded, files by `standard::fast-content-type` (a guess from the name
only, so a big folder lists quickly): `image/*` → image, plain text and
Markdown → text, other `text/plain` subtypes (C, Python, …) → code, the
rest → file. A folder's icon follows `notify::expanded` on its row; the
handler is connected in `on_bind_row ()` and disconnected in
`on_unbind_row ()`, since row widgets are reused as the list scrolls.

## The editor's color scheme

Dark mode uses `palang-dark` (`data/styles/palang-dark.xml`), light mode
GtkSourceView's own `Adwaita` (`update_style_scheme ()` in
`src/editor/palang-editor.c`). `palang-dark` sets
`parent-scheme="Adwaita-dark"`: it lists only what it changes (background,
text, line numbers, cursor, selection, the `def:` styles every language
maps onto), the rest comes from Adwaita Dark.

**A workaround:** GtkSourceView 5.12 finds schemes in a resource folder
but can't load them from there (it opens the name as a plain file). So
`add_palang_style_schemes ()` (called once from the editor's
`class_init`) copies each bundled scheme to `~/.cache/palang/styles/` and
adds that folder to the search path. To add a scheme: the XML, a line in
`palang.gresource.xml`, and its file name in that function's `names[]`.

The terminal's background is set to the same `#0f0f0f` in
`src/terminal/palang-terminal.c` (`update_colors ()`).

## Selections like VS Code (`style.css`)

- `tabbar tab:selected`: the editor's background (`@view_bg_color`), so
  the tab looks joined to the text, and a 2 px amber line on top (an
  `inset` box shadow). Other tabs' text is dimmed. The tab box has no
  vertical padding, so the selected tab touches the editor.
- `listview > row:selected`: an amber tint and a 2 px amber bar on the
  left; every list (file tree, search results, Problems, palette).
- `viewswitcher button.toggle:checked`: the panel's shown page is
  underlined.
- `.dim-label`: 75% instead of 55%.

## The two fixes

- **File tree, one click** (`on_row_pressed ()` in `palang-file-tree.c`):
  a `GtkGestureClick` on each row's contents replaces the list's
  `single-click-activate`, which selected rows on hover (CLAUDE.md gotcha
  20). It *claims* the click (`gtk_gesture_set_state (…, CLAIMED)`), so
  the list's own click handling doesn't run: otherwise a double click
  would also activate the row and fold a folder straight back. Because
  of that, it selects and focuses the row itself
  (`gtk_list_view_scroll_to (…, GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT, …)`).
  `style.css` moves the row's right padding into the contents so the
  whole row is clickable.
- **Save As** (`ask_for_location ()` in `src/app/palang-save-flow.c`): a
  document that was never saved starts in the window's project folder
  (`gtk_file_dialog_set_initial_folder ()`).

## For later phases

- **Ghost text (Phase 3)** will need a color that reads on `#0f0f0f`:
  give its tag a mid grey (e.g. `#6f6f6f`) and italic, not the scheme's
  comment color (`#7f8c8d`), so a suggestion isn't mistaken for a
  comment. Put the color in the code that creates the tag, or in CSS with
  a `palang-…` class.
- **A status bar** (Phase 3's token meter) doesn't exist yet; it would be
  a new widget at the bottom of the window, styled with the same named
  colors.
- **Math blocks (Phase 5)** can use `@accent_color` for their markers.
