# Phases 0–1 — as planned (done)

The original plan for the finished phases, kept for reference. What was
actually built, and how it differed, is in PROGRESS.md.

### Phase 0 — Project setup
- Install system deps; create the Meson project with the directory layout above.
- "Hello window": `PalangApplication` + `PalangWindow` with a single GtkSourceView.
- Debug build with sanitizers on by default: `meson setup build -Dbuildtype=debug -Db_sanitize=address,undefined`, `ninja -C build`, `meson test -C build`.
- `docs/00-how-gtk-apps-work.md`: short primer on GObject, signals, reference counting and the async/finish pattern, written for sahi.

### Phase 1 — MVP: editor + file tree + tabs
- **Window layout:** header bar, left file tree, center tab view (`adw::TabView`/`TabBar`), optional bottom panel placeholder.
- **File tree:** open folder, lazy-loaded `GtkTreeListModel` + `GtkListView` over `GtkDirectoryList`, icons, hidden-file toggle, refresh on disk changes (`GFileMonitor`).
- **Tabs:** `AdwTabView` + `AdwTabBar`; open/close/reorder, dirty indicator, "unsaved changes" prompt on close/quit.
- **Editor:** language auto-detect (`GtkSourceLanguageManager`), style schemes, line numbers, current-line highlight, auto-indent, bracket matching, tab width/spaces settings.
- **File ops:** New, Open, Save, Save As, Save All (`GtkSourceFileLoader` / `GtkSourceFileSaver`, async), reload when changed on disk.
- **Search:** find/replace in file (`GtkSourceSearchContext`), go to line.
- **Theme (light/dark):** a **System / Light / Dark** setting (`AdwStyleManager:color-scheme`). "System" uses the desktop's dark-mode preference when it reports one. Some desktops don't: MATE (sahi's desktop) reports *no preference* and switches themes only by GTK theme name, which libadwaita ignores (found in Phase 0 manual test 2). Fallback: when the desktop reports no preference, watch `GtkSettings:gtk-theme-name` (MATE updates it live) and use dark when the name contains "dark". *(Manual test: this still didn't switch on MATE, cause unknown; the default is Dark for now. See PROGRESS.md, Phase 1.)*
- **Settings:** `config.ini` via `GKeyFile` (font, theme, tab size, keybindings), basic preferences dialog (`AdwPreferencesDialog`).
- **Session restore:** reopen last folder and tabs.
- **Packaging:** first `.deb` with desktop entry and icon.

### Verification (Phase 1)

open a real project folder; edit/save/reopen files in several languages; close with unsaved changes → prompt; external modification → reload prompt; restart → session restored; install the `.deb` with `sudo apt install ./palang_*.deb` and launch from the menu.
