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

## Results notes

<!-- Write anything unexpected here, e.g. "Test 2: theme didn't switch live". -->

- Test 2: sahi's desktop is MATE, which reports no dark-mode preference to
  apps, so libadwaita never switches. Moved to Phase 1: a System/Light/Dark
  setting with a GTK-theme-name fallback (see PLAN.md, Phase 1).
