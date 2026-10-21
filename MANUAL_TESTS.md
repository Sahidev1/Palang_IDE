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
- [x] `tools/build-deb.sh`, then `sudo apt install ./build-deb/palang_0.1.0-1_amd64.deb`.
      Palang appears in the menu under Programming/Development, with the
      `</>` icon, and starts from there.
- [x] In the file manager, right-click a `.c` or `.txt` file → Open With →
      Palang. It opens in a tab.
      
      TEST NOTES: Opening files with 'palang' command in terminal seems to work as well.

### 2. Open and Save As dialogs
- [x] Ctrl+O: select two files at once → both open in tabs.
- [x] Ctrl+N, type something, Ctrl+S → the Save As dialog appears; save as
      `test.py`. The tab shows `test.py` and the text gets Python highlighting.
- [x] Ctrl+Shift+S on that file → save under another name; the tab follows.

### 3. Light / dark on MATE
- [ ] Preferences (Ctrl+,) → Style: Follow System. Switch MATE's theme to a
      dark one (e.g. Mint-Y-Dark) → Palang turns dark without restarting.
      Switch back → light again.
- [x] Style: Dark / Light override the system theme.

      TEST NOTES: first test fails. I know my default theme is a weird winxp theme for mate, but i switched to a standard system MATE theme that was dark, Palang theme remained light. So this is still not working, But I think it is low priority issue, I would for now resolve it by default to dark theme and not follow system theme. 

### 4. Font and indentation
- [x] Preferences → turn off "Use System Monospace Font", pick a font →
      every open editor changes immediately.
- [x] Change Tab Width to 2 → indentation in open editors changes.
- [x] Quit and start again → the choices are still there.

      TEST NOTES: all tests pass. but a bit annoying thing: If press Tab, it indents, but then if immediately press backspace, it does not undo indent, instead acts as regular backspace. This is minor issue and for now very low priority but could be to document it as future TODO's perhaps.

### 5. Tabs and sidebar
- [x] Drag a tab to reorder it.
- [x] F9 hides/shows the folder sidebar; the eye button shows hidden files.

### 6. Files changed by other programs
- [x] Open a file, then in a terminal run `echo "more" >> thatfile` →
      the new line appears in Palang by itself.
- [x] Type something (tab shows •), then run the same command → a banner
      appears and your text stays. Click Reload → the disk version appears.
- [x] Delete an open file in the terminal → banner "was deleted"; Ctrl+S
      brings it back.



### 7. Session
- [x] Open a folder and three files, select the middle tab, quit.
      Start Palang from the menu → same folder, tabs in the same order,
      middle tab selected.

### 8. Big files
- [x] `seq 1 300000 > /tmp/big.txt`, open it. Scrolling, typing, Ctrl+G
      `250000`, and Ctrl+F `29999` stay responsive.

---

## Phase 2a

Terminal, the `palang` command, project search, command palette and Quick
Open. These need real key presses and your real desktop. Run Palang with
`ninja -C build run` from the project folder, and open the project folder
in it (Ctrl+Shift+O) unless a test says otherwise.

### 1. Bottom panel
- [x] Ctrl+J shows the panel under the tabs with the Terminal page; Ctrl+J
      again hides it and the cursor is back in the text. The × in the
      panel and the terminal button in the header bar do the same.
- [x] Drag the divider to make the panel taller, hide and show it: same
      height. Quit and start again: the panel is shown (or not) as you left
      it, at that height, on the same page.
      
      

### 2. Terminal
- [x] The prompt is in the project folder (`pwd`), with your usual shell
      and prompt.
- [x] Keys go to the shell: type `echo one two`, press **Ctrl+W** → only
      `two` is deleted, no tab closes. **Ctrl+R** searches the history,
      **Ctrl+C** stops `sleep 100`, **Ctrl+L** clears.
- [x] These still work from the terminal: Ctrl+J, F9, Ctrl+Shift+F,
      Ctrl+Shift+P, Ctrl+P, Ctrl+, and Ctrl+Q.
- [x] Select text with the mouse, Ctrl+Shift+C, then Ctrl+Shift+V pastes
      it. Text copied in another program pastes with Ctrl+Shift+V.
- [x] `exit` → "Process exited — press Enter to restart"; Enter gives a new
      prompt.
- [x] Preferences → change the font or the style: the terminal follows.
- [x] Run `htop` or `mc` (if installed): colours and mouse look right;
      `vim` works.
    
    TEST NOTES: test 2, 3rd one: It passes, but alot of them if run command after already done it repetadly it all stacks, so then have to esc from multiple of it. for example ctrl+shift+p repeateadly stacks multiple new windows on top eachother, and have to press esc as many times to close all. 
    

### 3. The `palang` command
- [x] In the Palang terminal: `palang help`, `palang settings`,
      `palang get tab-width`.
- [x] `palang set tab-width 2`: open editors use 2 from now on, and
      Preferences shows 2. `palang reset tab-width` → back to 4.
- [x] `palang set color-scheme purple; echo $?` → an error listing
      system, light, dark, and `1`.
- [x] `palang set color-scheme light` → the window turns light at once.
- [x] `palang README.md` opens the file in a tab; `palang ./set` would open
      a file called `set`.
- [x] In a terminal *outside* Palang (with Palang running),
      `./build/src/palang settings` doesn't list settings: Palang tries to
      open a file called `settings` ("Could not open file" if there's none).
- [x] After installing the `.deb` (Phase 1 test 1): in the Palang terminal,
      `palang set <Tab><Tab>` lists the setting names and
      `palang set color-scheme <Tab><Tab>` lists system, light, dark.

    TEST NOTES: all tests pass but strange behavior with tab width = 2: Tabing from empty line works, but type something, and tab, it only goes one space, not a tab forward. Minor issue, but worth nothing.

### 4. Project search
- [x] Select a word in an editor, Ctrl+Shift+F: the Search page opens
      searching for it. Results are grouped under file paths, with the
      matches in bold.
- [x] Click a result: its file opens (or its tab is selected) with the
      cursor on that line, scrolled into view.
- [x] Try Aa, W and .*; an invalid regex like `foo(` with .* on shows an
      error message instead of results.
- [x] Search for something very common (`e`): it stops at 5000 results and
      says so; the window stays responsive while it searches.
- [x] Files ignored by `.gitignore` (e.g. under `build/`) aren't searched.

### 5. Command palette and Quick Open
- [x] Ctrl+Shift+P: every command with its shortcut. Type `sva` → Save All
      on top; Up/Down move the highlight; Enter runs it; Escape closes.
- [x] Ctrl+P: type `appwin` → palang-window.c and .h (src/app); Enter
      opens the highlighted one. A click opens one too.
- [x] Ctrl+P with no folder open shows "Open a folder first".

---

## Results notes


<!-- Write anything unexpected here, e.g. "Test 2: theme didn't switch live". -->

- Test 2: sahi's desktop is MATE, which reports no dark-mode preference to
  apps, so libadwaita never switches. Moved to Phase 1: a System/Light/Dark
  setting with a GTK-theme-name fallback (see docs/plan/phase-0-1.md, Phase 1).
- Phase 1, test 3 (Follow System on MATE): failed, Palang stayed light with
  Mint-Y-Dark. Decided: low priority; the default style is now Dark, and
  the issue is recorded in PROGRESS.md (Phase 1, manual test results).
- Phase 1, test 4 (Backspace after Tab removed one space, not the indent):
  fixed by turning on GtkSourceView's smart-backspace.
- Phase 2a, test 2 (shortcuts in the terminal): pressing Ctrl+Shift+P (or
  Ctrl+P, Ctrl+,) again opened another dialog on top, each needing its own
  Escape. Fixed: one palette at a time (the other palette's shortcut
  switches to it), one Preferences. Writing its test also found a crash
  in Quick Open for a folder with no files (fixed).
- Phase 2a, test 3 (tab width 2: Tab after typing moved one space): not a
  bug. With spaces instead of tabs, Tab moves to the next tab stop (every
  2 columns), so after one letter that is one space; a tab character, VS
  Code and gedit do the same.
