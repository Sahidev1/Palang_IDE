# Icons

- `io.github.sahidev1.Palang.svg`: the app icon, installed into the
  system's icon theme for the menu and the taskbar (`data/meson.build`).
  Its source and drafts are in `design/logo/`.
- `scalable/actions/palang-…-symbolic.svg`: the icons inside the app,
  bundled into the binary (`palang.gresource.xml`). GTK looks for icons
  under the app's resource path + `/icons` by itself, so a widget uses one
  by name: `palang-close-symbolic`.

## Drawing a UI icon

A **symbolic** icon (name ending in `-symbolic`) is recoloured by GTK to
match the theme, so the same file works in light and dark:

- Every shape gets the text colour, whatever `fill` the file says
  (`#2e3436` is only what an SVG viewer shows).
- A shape with `class="warning"` gets the theme's warning colour (amber),
  `class="error"` its error colour (red), `class="success"` green. That is
  how the icons have an amber accent. It also works where no style
  applies, e.g. the editor's gutter, which is why the gutter's error and
  warning icons are now red and amber.
- Use filled shapes, not strokes: GTK recolours fills only. Holes come
  from `fill-rule="evenodd"` (a shape inside another cuts it out).

Style: a 16×16 grid, corners cut at 45°, at most one amber detail. Check a
change with `design/icons/render-sheet.sh`.
