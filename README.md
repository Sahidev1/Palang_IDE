# Palang IDE

A lightweight, native IDE for Linux (Debian family), written in C with GTK4,
libadwaita and GtkSourceView 5. See [PLAN.md](PLAN.md) for the roadmap,
[ARCHITECTURE.md](ARCHITECTURE.md) for how the code is organised,
[PROGRESS.md](PROGRESS.md) for what's done, and [docs/](docs/) for how the
code works.

## Features (Phase 1)

- Tabs, syntax highlighting for 150+ languages, line numbers
- Folder tree sidebar that follows changes on disk
- Find / Replace (case, whole word, regex), Go to Line
- Light / dark style (also on desktops like MATE), font and indentation settings
- Prompts before losing unsaved work; reloads files changed by other programs
- Restores the open folder and files on the next start

## Keyboard shortcuts

| Action | Keys |
|---|---|
| New / Open file / Open folder | Ctrl+N / Ctrl+O / Ctrl+Shift+O |
| Save / Save As / Save All | Ctrl+S / Ctrl+Shift+S / Ctrl+Alt+S |
| Close tab / Quit | Ctrl+W / Ctrl+Q |
| Find / Replace | Ctrl+F / Ctrl+H |
| Next / previous match | F3 / Shift+F3 |
| Go to line | Ctrl+G |
| Show / hide folder sidebar | F9 |
| Preferences | Ctrl+, |

## Build and run

Dependencies (Debian / Ubuntu / Linux Mint):

```sh
sudo apt install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libgtksourceview-5-dev libadwaita-1-dev \
  xvfb xauth desktop-file-utils
```

Development build (with AddressSanitizer and UBSan):

```sh
meson setup build
ninja -C build
ninja -C build run      # runs Palang with the sanitizer settings
meson test -C build     # UI tests use xvfb-run when installed
```

Open files or a folder: `./build/src/palang file.c` or
`./build/src/palang ~/projects/foo`.

## Install as a package

```sh
tools/build-deb.sh                          # builds build-deb/palang_*.deb
sudo apt install ./build-deb/palang_*.deb   # then start Palang from the menu
```

`tools/build-deb.sh` packages the last commit (uncommitted changes are not
included).

## Files Palang uses

- `~/.config/palang/config.ini` — preferences
- `~/.local/state/palang/session.ini` — what was open last time
