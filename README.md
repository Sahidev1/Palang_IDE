# Palang IDE

A lightweight, native IDE for Linux (Debian family), written in C with GTK4,
libadwaita and GtkSourceView 5. See [PLAN.md](PLAN.md) for the roadmap,
[ARCHITECTURE.md](ARCHITECTURE.md) for how the code is organised,
[PROGRESS.md](PROGRESS.md) for what's done, and [docs/](docs/) for how the
code works.

## Features

- Tabs, syntax highlighting for 150+ languages, line numbers
- Folder tree sidebar that follows changes on disk
- Find / Replace (case, whole word, regex), Go to Line
- Light / dark style (also on desktops like MATE), font and indentation settings
- Prompts before losing unsaved work; reloads files changed by other programs
- Restores the open folder and files on the next start
- Integrated terminal; in it, `palang set NAME VALUE` changes settings
  (`palang help` lists the commands)
- Project-wide search (needs ripgrep), command palette, Quick Open
- Language servers for C/C++ (clangd), Python (pylsp) and
  JavaScript/TypeScript (typescript-language-server): errors and warnings
  in the text and on a Problems page, go to definition, hover
  documentation, completion with snippets
- Its own look: the leopard logo, angular icons, an amber accent and a
  high-contrast dark style
- A complaint box (Ctrl+Shift+F1) that writes what bothers you, with the
  time and the file and line you were in, into a Markdown file

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
| Show / hide terminal and search panel | Ctrl+J |
| Search in the folder | Ctrl+Shift+F |
| Go to definition | F12 (or Ctrl+click) |
| Show completions | Ctrl+Space |
| Show problems | Ctrl+Shift+M |
| Complain about Palang | Ctrl+Shift+F1 (Ctrl+Enter sends) |
| Command palette / Open file in folder | Ctrl+Shift+P / Ctrl+P |
| Copy / paste in the terminal | Ctrl+Shift+C / Ctrl+Shift+V |
| Preferences | Ctrl+, |

## Build and run

Dependencies (Debian / Ubuntu / Linux Mint):

```sh
sudo apt install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libgtksourceview-5-dev libadwaita-1-dev \
  libvte-2.91-gtk4-dev libjson-glib-dev libjsonrpc-glib-1.0-dev ripgrep \
  xvfb xauth desktop-file-utils
```

Language servers are optional; install the ones you need:

```sh
sudo apt install clangd python3-pylsp python3-pyflakes
npm install -g typescript-language-server typescript@5
```

Their commands are settings (`c-server`, `python-server`,
`javascript-server`); `palang settings` in Palang's terminal lists them.
A server installed with nvm is on the PATH only in terminals: started from
the menu, Palang needs its full path, e.g.
`palang set javascript-server "$(command -v typescript-language-server) --stdio"`.

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
- `~/.local/share/palang/complaints.md` — the complaint box's file (the
  `complaints-file` setting can point elsewhere)
- `~/.cache/palang/styles/` — Palang's editor color schemes, copied there
  at start
