palang_deps = [gtk_dep, adw_dep, gtksource_dep, vte_dep]
palang_inc  = include_directories('app', 'editor', 'palette', 'settings', 'terminal',
                                  'workspace')

# Everything except main.c lives in a static library, so unit tests can link
# against the same code the app uses.
libpalang = static_library('palang',
  'app/palang-application.c',
  'app/palang-panel.c',
  'app/palang-preferences-dialog.c',
  'app/palang-save-flow.c',
  'app/palang-session.c',
  'app/palang-window.c',
  'editor/palang-document.c',
  'editor/palang-editor.c',
  'editor/palang-search-bar.c',
  'palette/palang-command.c',
  'settings/palang-appearance.c',
  'settings/palang-settings.c',
  'terminal/palang-terminal.c',
  'workspace/palang-directory-model.c',
  'workspace/palang-file-tree.c',
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)

# The resources go into the executable itself rather than the static library:
# they register themselves through a constructor function that nothing calls
# directly, so the linker would drop them from a library.
palang = executable('palang',
  'main.c',
  palang_resources,
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
  install: true,
)

# `meson compile -C build run` (or `ninja -C build run`) starts Palang with the
# sanitizer settings above.
run_target('run',
  command: palang,
  env: sanitizer_env,
)
