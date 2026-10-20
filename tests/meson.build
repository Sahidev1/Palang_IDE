test_application = executable('test-application',
  'test-application.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('application', test_application, env: sanitizer_env)

test_document = executable('test-document',
  'test-document.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('document', test_document, env: sanitizer_env)

# UI tests need a display. Use a virtual one (xvfb-run) when installed, so
# they also work without a desktop session; otherwise use the current one.
xvfb_run = find_program('xvfb-run', required: false)

test_window = executable('test-window',
  'test-window.c',
  palang_resources,
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)

if xvfb_run.found()
  test('window', xvfb_run, args: ['-a', test_window], env: sanitizer_env)
else
  test('window', test_window, env: sanitizer_env)
endif
