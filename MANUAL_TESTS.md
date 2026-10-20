# Manual Tests

Checks that need a person at the keyboard. Tick each box (`[ ]` → `[x]`) when
it passes. If one fails, write what happened under it.

Start Palang with `ninja -C build run` unless a test says otherwise.

---

## Phase 0

### 1. Ctrl+Q quits
- [x] With the Palang window focused, press **Ctrl+Q**. The window closes and the
      terminal shows no error output.

### 2. Light/dark theme follows the system
- [ ] With Palang open, switch Mint's theme between light and dark
      (System Settings → Themes, or the "Dark/Light" option if your Mint
      version has one). The editor's colours switch to match without restarting.
	TEST FAILED. Changing to dark theme -> nothing at all happened to Palang window
- [ ] Close Palang, switch the theme, start Palang again. It starts in the
      matching colours.
	TEST FAILED. changing to dark theme -> nothing happeed starting it again.

> If nothing changes: Cinnamon may not send the dark-mode signal that
> libadwaita listens for. That's a desktop limitation, not a crash. Note it
> here; Phase 1 can then add a manual light/dark setting.

### 3. A second launch reuses the window
- [x] With Palang already open, run `ninja -C build run` in a second terminal.
      No second window appears; the existing window comes to the front. The
      second command returns right away.
	PASSED on retest. The first attempt needed two clicks on the close button:
	the second terminal had been started as a background child of the first.
	From a fresh terminal it works. The leak report in .ERRDUMP.txt came from
	IBus (the input-method library), not Palang; now suppressed in
	tools/lsan.supp.
---

## Phase 1

These cover what automated tests can't: system dialogs, the desktop menu,
your real theme, and how it feels. Run Palang with `ninja -C build run`
unless a test says otherwise.

### 1. Install the package
- [ ] `tools/build-deb.sh`, then `sudo apt install ./build-deb/palang_0.1.0-1_amd64.deb`.
      Palang appears in the menu under Programming/Development, with the
      `</>` icon, and starts from there.
- [ ] In the file manager, right-click a `.c` or `.txt` file → Open With →
      Palang. It opens in a tab.

### 2. Open and Save As dialogs
- [ ] Ctrl+O: select two files at once → both open in tabs.
- [ ] Ctrl+N, type something, Ctrl+S → the Save As dialog appears; save as
      `test.py`. The tab shows `test.py` and the text gets Python highlighting.
- [ ] Ctrl+Shift+S on that file → save under another name; the tab follows.

### 3. Light / dark on MATE
- [ ] Preferences (Ctrl+,) → Style: Follow System. Switch MATE's theme to a
      dark one (e.g. Mint-Y-Dark) → Palang turns dark without restarting.
      Switch back → light again.
- [ ] Style: Dark / Light override the system theme.

### 4. Font and indentation
- [ ] Preferences → turn off "Use System Monospace Font", pick a font →
      every open editor changes immediately.
- [ ] Change Tab Width to 2 → indentation in open editors changes.
- [ ] Quit and start again → the choices are still there.

### 5. Tabs and sidebar
- [ ] Drag a tab to reorder it.
- [ ] F9 hides/shows the folder sidebar; the eye button shows hidden files.

### 6. Files changed by other programs
- [ ] Open a file, then in a terminal run `echo "more" >> thatfile` →
      the new line appears in Palang by itself.
- [ ] Type something (tab shows •), then run the same command → a banner
      appears and your text stays. Click Reload → the disk version appears.
- [ ] Delete an open file in the terminal → banner "was deleted"; Ctrl+S
      brings it back.

### 7. Session
- [ ] Open a folder and three files, select the middle tab, quit.
      Start Palang from the menu → same folder, tabs in the same order,
      middle tab selected.

### 8. Big files
- [ ] `seq 1 300000 > /tmp/big.txt`, open it. Scrolling, typing, Ctrl+G
      `250000`, and Ctrl+F `29999` stay responsive.

---

## Results notes

<!-- Write anything unexpected here, e.g. "Test 2: theme didn't switch live". -->

- Test 2: sahi's desktop is MATE, which reports no dark-mode preference to
  apps, so libadwaita never switches. Moved to Phase 1: a System/Light/Dark
  setting with a GTK-theme-name fallback (see PLAN.md, Phase 1).
