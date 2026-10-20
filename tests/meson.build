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
