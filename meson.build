project('palang', 'c',
  version: '0.1.0',
  meson_version: '>= 1.0.0',
  default_options: [
    'c_std=gnu17',
    'warning_level=2',          # -Wall -Wextra
    'werror=true',
    'buildtype=debug',
    # Catch memory bugs (use-after-free, overruns, leaks) and undefined
    # behaviour while developing. Release builds pass -Db_sanitize=none.
    'b_sanitize=address,undefined',
  ],
)

cc = meson.get_compiler('c')

# GTK callbacks often receive parameters they don't need, and GLib structs
# are commonly initialized partially, so these two warnings are just noise.
add_project_arguments(
  cc.get_supported_arguments([
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
  ]),
  language: 'c',
)

gnome = import('gnome')

gtk_dep       = dependency('gtk4', version: '>= 4.14')
adw_dep       = dependency('libadwaita-1', version: '>= 1.5')
gtksource_dep = dependency('gtksourceview-5', version: '>= 5.12')

# config.h: values the C code needs from the build system.
conf = configuration_data()
conf.set_quoted('APPLICATION_ID', 'io.github.sahidev1.Palang')
conf.set_quoted('RESOURCE_PATH', '/io/github/sahidev1/Palang')
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
configure_file(output: 'config.h', configuration: conf)
config_inc = include_directories('.')

# Environment for running Palang and its tests under the sanitizers.
lsan_options = 'suppressions=' + meson.project_source_root() / 'tools' / 'lsan.supp'

sanitizer_env = environment()
sanitizer_env.set('LSAN_OPTIONS', lsan_options, 'print_suppressions=0')
sanitizer_env.set('UBSAN_OPTIONS', 'print_stacktrace=1', 'halt_on_error=1')

# Tests additionally get their own config/data folders inside the build
# directory, so they never read or change the user's real Palang settings
# or session. (A separate environment(), so `ninja run` isn't affected.)
test_home = meson.project_build_root() / 'test-home'
test_env = environment()
test_env.set('LSAN_OPTIONS', lsan_options, 'print_suppressions=0')
test_env.set('UBSAN_OPTIONS', 'print_stacktrace=1', 'halt_on_error=1')
test_env.set('XDG_CONFIG_HOME', test_home / 'config')
test_env.set('XDG_DATA_HOME', test_home / 'data')
test_env.set('XDG_STATE_HOME', test_home / 'state')
test_env.set('XDG_CACHE_HOME', test_home / 'cache')

subdir('data')
subdir('src')
subdir('tests')
