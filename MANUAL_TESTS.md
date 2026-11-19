# Manual Tests

Checks that need a person at the keyboard. Tick each box (`[ ]` → `[x]`) when
it passes. If one fails, write what happened under it.

Start Palang with `ninja -C build run` unless a test says otherwise.

## Status (2026-11-19)

Every section up to "Complaint fixes" has been run by sahi. All passed
except following the system's light/dark on MATE (Phase 0 test 2,
Phase 1 test 3: low priority, default style is Dark).
New phases add their section at the end, before "Results notes".

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

## Phase 2b

Language servers. Run Palang with `ninja -C build run` from the project
folder and open the project folder in it (Ctrl+Shift+O). Needs clangd,
pylsp with pyflakes, and typescript-language-server with TypeScript 5
(see PROGRESS.md, Phase 2b, "Changes to the plan"). For the Python and
JavaScript tests, make small files in a scratch folder outside the
project, e.g. `/tmp/lsp-test/`.

### 1. C with clangd (Palang's own code)
- [x] Open `src/main.c`, then type `int x = y;` on a new line inside
      `main`. After a moment `y` gets a red wavy underline and the line a
      gutter icon. Delete the line: they go away.
- [x] Rest the mouse pointer on the underlined `y`: a popup says
      "Error: Use of undeclared identifier 'y' (clang)". It looks like a
      normal popup (no black box around it).
- [x] Put the cursor on `palang_application_new` and press **F12**:
      `palang-application.c` opens at its definition, scrolled so you
      can see it.
- [x] **Ctrl+click** `g_application_run`: GLib's `gapplication.h` opens at
      its declaration. A plain click (no Ctrl) just moves the cursor.
- [x] Ctrl+Shift+P → "Go to Definition" does the same as F12.

    TEST NOTES: Last test bug detected, when ctrl+shift+p, if mouse cursor is within the window, and you press down, at bottom moves down 1 line, then loops over to top
    and repeats. but remove mouse cursor from command window and it works fine.

### 2. Python and JavaScript
- [x] A `.py` file with `import os` and `z = undefined_name`: an orange
      underline under the unused import, a red one under
      `undefined_name`.
- [x] A `.js` file with `return x +;` inside a function: a red underline
      at the `;`.
- [x] In the `.py` file, define `def f(): pass` and call `f()` further
      down; F12 on the call jumps to the definition.

### 3. Servers missing or broken
- [x] In the Palang terminal: `palang set python-server pylsp-missing`.
      Open (or reopen) a `.py` file: a toast says the server “pylsp-missing”
      wasn't found. **Details** shows the whole message; the text can be
      selected and copied. `palang reset python-server`, then Ctrl+Shift+P
      → "Restart Language Servers": the diagnostics come back.
- [x] Start Palang from the **desktop menu** (or a `.desktop` launcher),
      not a terminal, and open a `.js` file. Does the JavaScript server
      start (diagnostics appear), or does a toast say it wasn't found?
      (nvm puts Node only on the terminal's `PATH`; write down which.)

    TEST NOTES: Last test got a server language error: The JavaScript language server “typescript-language-server” isn't installed. Install it with: npm install -g typescript-language-server typescript@5. 

    TEST 2 NOTES: Last test, initially didnt work but i ran script in error message. Then after it worked.

### 4. Quitting
- [x] With C, Python and JavaScript files open, quit with Ctrl+Q: Palang
      closes within about two seconds, and `pgrep -a 'clangd|pylsp|typescript'`
      in another terminal shows no servers left.

## Phase 2c

Hover, completion and the Problems page. Run Palang with
`ninja -C build run`, open the project folder (Ctrl+Shift+O), and for
Python and JavaScript use small files in a scratch folder as in Phase 2b
(JavaScript: start Palang from a terminal, or set the full path as the
toast's Details say).

### 1. Hover
- [X] In `src/main.c`, rest the pointer on `palang_application_new`: a
      popup with a bold "function palang_application_new", the header it
      comes from, and the signature in a monospace font.
- [X] In a `.py` file with `import json`, rest it on `json`: the
      warning ('json' imported but unused) on top, a line, then the
      module's documentation, which scrolls when long.

### 2. Completion
- [X] Inside `main` in `src/main.c`, type `g_appl`: a list of
      `g_application_…` entries opens by itself, with their return type
      on the right and the typed letters in bold. Down/Up move, Enter
      inserts; one Ctrl+Z takes it back.
- [X] In a `.js` file, `const xs = [1];` then on the next line `xs.ma`:
      `map` comes first. In a `.py` file, `import os` then `os.pa`:
      `path` and `pardir` are there.
- [X] Type `PRI` in a C file, press Escape (the list closes), then
      Ctrl+Space: does the list open at once, or only on the second
      Ctrl+Space? (On the virtual display only the second worked: see
      PROGRESS.md, Phase 2c, open items. On some desktops Ctrl+Space
      switches the input method instead: write down what happens.)

      NOTES: CTRL+SPACE twice was needed to see list again

- [X] Type `PRIX` then `8`: `PRIX8` is above `PRIx8` (the exact case
      comes first).
- [X] Type a whole `return` and press Enter: a new line, not a
      completion. `s.b` (with a struct `s` that has a field `beta`) +
      Enter still inserts `beta`.

### 3. Snippets and details
- [X] In a C file with `#include <stdio.h>`, type `prin` and press Tab:
      `printf(…)` with the argument selected. Type `42`, press Tab: the
      cursor jumps past the `)`.
- [X] Type `mit` and press Tab (with no list open, e.g. after Escape):
      just a tab or spaces, no license text.
- [X] With a list open, click **Details**: a pane with the signature and
      documentation (in `.js`, after `xs.ma`, it appears after a moment).
      Does it sit well next to the list (no big overlap)? It stays open
      for the next lists.
- [X] In a C file *without* `#include <inttypes.h>`, type `PRIX8` and
      pick the entry marked •: `#include <inttypes.h>` appears at the
      top too; one Ctrl+Z removes both.

### 4. Problems page
- [X] Open a `.c` and a `.py` file with errors, press Ctrl+Shift+M: the
      Problems page lists them (icons, message, "clang · file:line"),
      sorted by file.
- [X] Click a row: its file opens with the cursor at the problem. Fix
      the error: its row goes away. Close the tab: its rows go away.
- [X] Ctrl+Shift+M also works while the terminal has the focus.

## Interim — Look and feel

The round between Phase 2 and 3 (PROGRESS.md, "Interim"). Start with
`ninja -C build run`; for the app icon, install the `.deb` first
(`tools/build-deb.sh`, then install the newest file in `build-deb/`).

### 1. Logo and icons
- [X] After installing the `.deb`: the menu entry and the taskbar show the
      amber leopard on a dark tile.
- [X] With no file open, the big leopard shows above "No Open Files".
- [X] The header bar, find bar (Ctrl+H), panel tabs and close buttons show
      Palang's angular icons, with amber details (sidebar strip, `+`,
      prompt `>`, replace arrow).
- [X] In the file tree, folders show a closed folder and an amber open
      folder when expanded; `.c`/`.py` files a code icon, README/`.md` a
      text icon, images an image icon.
- [X] In a C file with an error, the gutter icon is red; a Python
      warning's is amber (they used to be grey).

### 2. Colors, corners, frame
- [X] The window has a thin double-line frame and square corners; when
      maximized, no frame.
- [X] Buttons, text fields, tabs, menus, the palette and Preferences have
      square corners.
- [X] Preferences (Ctrl+,): switches are amber, not blue. Switch Style to
      Light and back: the amber gets deeper in light mode; everything
      stays readable.
- [X] Dark mode: the editor and the terminal are near-black, code colors
      are brighter (orange keywords, teal types, green strings), the
      current line number is amber.
- [X] With two files open, the selected tab joins the editor with an
      amber line on top; the other tab is dimmed.
- [X] Selected rows (file tree, Problems, palette) are tinted amber with
      a bar on the left; the shown panel page is underlined.
- [ ] Anything too bright, too dark or hard to read? Write it below.

### 3. The two fixes
- [X] Move the pointer over several files in the tree, then away: no row
      stays highlighted. One click opens a file or folds a folder; a
      double click on a folder doesn't fold it back.
- [X] Open a project folder, Ctrl+N, type something, Ctrl+S: the Save As
      dialog starts in the project folder.

## Complaint box

Added 2026-10-23. Run `ninja -C build run`. First, in Palang's terminal,
point it at the repo (once; it's saved):
`palang set complaints-file ~/Documents/projects/agentic_projects/Palang_IDE/COMPLAINTS.md`

- [X] With a file open, press Ctrl+Shift+F1: a "Complaint Box" dialog
      with "About <file>:<line>" (the cursor's line) and "Goes to
      ~/…/COMPLAINTS.md". Send is grey until you type something.
- [X] Type two lines (Enter makes a new line), press Ctrl+Enter: the
      dialog closes, "Complaint saved" shows, and `COMPLAINTS.md` has a
      heading at the top and your entry with the time.
- [X] Ctrl+Shift+F1 also works while the terminal has the focus, and from
      the command palette ("Complain about Palang…").
- [X] In Palang's terminal: `palang complain the font is too small`:
      "Saved to …", entry marked "terminal".

## Complaint fixes

Added 2026-11-19: sahi's seven complaints (`COMPLAINTS.md`, each marked
"Handled in …"). Run `ninja -C build run` unless it says otherwise.

### 1. Menu icon and sidebar
- [X] After installing the new `.deb`: the MATE menu entry shows the
      amber leopard, not a generic image (if not at once, log out and in).
- [X] Open a folder: a faint amber leopard at the bottom of the sidebar,
      the folder name in bold amber with an amber line under it, a slight
      warm glow at the bottom. With a long tree, the names over the
      leopard are fully readable and clickable. Same in Light style.

### 2. Brackets and Enter
- [X] In a `.js` or `.c` file type `f(`: you get `f()` with the cursor
      inside. Type `"a", (1` then `)` `)` `;`: the result is
      `f("a", (1));` (the closers were stepped over, not doubled).
- [X] `x(` then Backspace: just `x`. `don't`: no extra quote. Select a
      word and type `"`: the word is wrapped in quotes.
- [X] Type `if (x) {` and press Enter: `}` moves to its own line, the
      cursor sits on an indented line in between. One Ctrl+Z undoes it.
- [X] With the completion list open (Ctrl+Space) between `{}`, Enter
      still takes the entry.
- [X] Preferences → Typing → "Close Brackets and Quotes" off: `(` stays
      alone. Turn it back on.

### 3. Select line, terminal focus
- [X] Ctrl+L selects the cursor's whole line; again adds the next line.
      In the terminal, Ctrl+L still clears the screen.
- [X] Ctrl+` (or **Ctrl+§**, the key left of 1 on a Swedish keyboard:
      couldn't be tested on the virtual display) from the editor: the
      panel opens on the Terminal and it has the focus. Again: back to
      the editor. Write down which of the two keys works for you.

      NOTES: CTRL+` does not work, but the other alternative did

### 4. Panel size from a terminal
- [X] In a terminal: `palang .` (the installed one). Open the panel
      (Ctrl+J), drag it much taller, quit with Ctrl+Q. `palang .` again:
      same height. Then from the menu: same height too.
- [X] Start `palang .` from a terminal, change the panel height, press
      Ctrl+C in that terminal: Palang closes (asking first if a file has
      unsaved changes; Cancel keeps it open). Start it again: the height
      is kept.

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
- Phase 2b, test 1 (palette: with the pointer over the list, Down at the
  bottom jumped back up): the list used `single-click-activate`, which in
  GTK4 also selects the row under the pointer; when Down scrolled the list,
  another row slid under the pointer and got selected. Fixed: one click is
  handled per row instead (`palang-palette.c`, `on_row_clicked`), and
  `data/style.css` makes the row's contents fill the whole row.
- Phase 2b, test 3 (JavaScript server from the desktop menu: “isn't
  installed”): expected. nvm puts Node on the PATH only in terminals.
  Decided (option A): a server given by its full path gets its own folder
  first on PATH (that's where nvm keeps `node`), and the toast's Details
  say how: in Palang's terminal,
  `palang set javascript-server "$(command -v typescript-language-server) --stdio"`.
- Phase 2c, Completion test 3 (Ctrl+Space right after Escape): on sahi's
  desktop too, the list only came back on the *second* Ctrl+Space. A
  real bug, now an open item (CLAUDE.md); not investigated yet.
- Complaint fixes, test 3 (terminal focus): Ctrl+` can't be pressed on
  sahi's Swedish keyboard (` is a dead key there); Ctrl+§ works. The
  command palette now shows the shortcut the keyboard has (`bb8bbd8`).
- Phase 2b, test 3 (JavaScript server from the menu), retest: worked
  after running the install command the error message gives.
- Complaint box: the setup step (`palang set complaints-file …`) wasn't
  done, so the two test complaints went to
  `~/.local/share/palang/complaints.md`, not the repo.
