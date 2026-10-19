# Palang IDE

A lightweight, native IDE for Linux (Debian family), written in C with GTK4,
libadwaita and GtkSourceView 5. See [PLAN.md](PLAN.md) for the roadmap and
[docs/](docs/) for how the code works.

## Build

Dependencies (Debian / Ubuntu / Linux Mint):

```sh
sudo apt install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libgtksourceview-5-dev libadwaita-1-dev
```

Build and run (debug build, with AddressSanitizer and UBSan):

```sh
meson setup build
ninja -C build
ninja -C build run      # runs Palang with the sanitizer settings
meson test -C build
```
