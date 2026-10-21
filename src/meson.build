palang_deps = [gtk_dep, adw_dep, gtksource_dep, vte_dep, json_dep, jsonrpc_dep]
palang_inc  = include_directories('app', 'editor', 'lsp', 'palette', 'search', 'settings',
                                  'terminal', 'workspace')

# Everything except main.c lives in a static library, so unit tests can link
# against the same code the app uses.
libpalang = static_library('palang',
  'app/palang-application.c',
  'app/palang-command-line.c',
  'app/palang-panel.c',
  'app/palang-preferences-dialog.c',
  'app/palang-save-flow.c',
  'app/palang-session.c',
  'app/palang-window.c',
  'app/palang-window-lsp.c',
  'app/palang-window-palette.c',
  'app/palang-window-panel.c',
  'editor/palang-document.c',
  'editor/palang-editor.c',
  'editor/palang-search-bar.c',
  'lsp/palang-lsp-client.c',
  'lsp/palang-lsp-diagnostics.c',
  'lsp/palang-lsp-document.c',
  'lsp/palang-lsp-hover-provider.c',
  'lsp/palang-lsp-languages.c',
  'lsp/palang-lsp-manager.c',
  'lsp/palang-lsp-position.c',
  'palette/palang-command.c',
  'palette/palang-fuzzy.c',
  'palette/palang-palette.c',
  'palette/palang-palette-item.c',
  'search/palang-rg.c',
  'search/palang-search.c',
  'search/palang-search-match.c',
  'search/palang-search-panel.c',
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
