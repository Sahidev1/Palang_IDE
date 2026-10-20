# Bundle UI files (and later CSS, icons) into the binary, so the app never
# has to find them on disk at runtime.
palang_resources = gnome.compile_resources('palang-resources',
  'palang.gresource.xml',
  c_name: 'palang',
)

# Desktop integration: menu entry and icon.
install_data('io.github.sahidev1.Palang.desktop',
  install_dir: get_option('datadir') / 'applications',
)
install_data('icons/io.github.sahidev1.Palang.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)

desktop_file_validate = find_program('desktop-file-validate', required: false)
if desktop_file_validate.found()
  test('desktop-file', desktop_file_validate,
    args: [meson.current_source_dir() / 'io.github.sahidev1.Palang.desktop'],
  )
endif
