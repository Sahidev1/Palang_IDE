test_application = executable('test-application',
  'test-application.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('application', test_application, env: test_env)

test_document = executable('test-document',
  'test-document.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('document', test_document, env: test_env)

test_settings = executable('test-settings',
  'test-settings.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('settings', test_settings, env: test_env)

test_session = executable('test-session',
  'test-session.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('session', test_session, env: test_env)

test_command_line = executable('test-command-line',
  'test-command-line.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('command-line', test_command_line, env: test_env)

test_search = executable('test-search',
  'test-search.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('search', test_search, env: test_env)

test_fuzzy = executable('test-fuzzy',
  'test-fuzzy.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('fuzzy', test_fuzzy, env: test_env)

# UI tests need a display. Use a virtual one (xvfb-run) when installed, so
# they also work without a desktop session; otherwise use the current one.
xvfb_run = find_program('xvfb-run', required: false)

# Tests that create widgets, so they need a display.
ui_tests = {
  'window': 'test-window.c',
  'file-tree': 'test-file-tree.c',
  'editor': 'test-editor.c',
  'appearance': 'test-appearance.c',
  'terminal': 'test-terminal.c',
  'palette': 'test-palette.c',
}

foreach name, source : ui_tests
  test_exe = executable('test-' + name,
    source,
    palang_resources,
    link_with: libpalang,
    dependencies: palang_deps,
    include_directories: [config_inc, palang_inc],
  )

  if xvfb_run.found()
    test(name, xvfb_run, args: ['-a', test_exe], env: test_env)
  else
    test(name, test_exe, env: test_env)
  endif
endforeach

# The language server tests talk to a fake server (fake-lsp-server.c); the
# test program learns its path at compile time.
fake_lsp_server = executable('fake-lsp-server',
  'fake-lsp-server.c',
  # gio-unix: GUnixInputStream, to read stdin as a GInputStream.
  dependencies: palang_deps + [dependency('gio-unix-2.0')],
  include_directories: [config_inc],
)

test_lsp = executable('test-lsp',
  'test-lsp.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
  c_args: ['-DFAKE_LSP_SERVER="@0@"'.format(fake_lsp_server.full_path())],
)
# It creates GtkSourceViews (snippet insertion), so it needs a display too.
if xvfb_run.found()
  test('lsp', xvfb_run, args: ['-a', test_lsp], env: test_env, depends: fake_lsp_server)
else
  test('lsp', test_lsp, env: test_env, depends: fake_lsp_server)
endif
