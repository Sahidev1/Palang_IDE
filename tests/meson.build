test_application = executable('test-application',
  'test-application.c',
  link_with: libpalang,
  dependencies: palang_deps,
  include_directories: [config_inc, palang_inc],
)
test('application', test_application, env: sanitizer_env)
