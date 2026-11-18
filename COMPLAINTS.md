# Complaints about Palang

Written from Palang's complaint box (Ctrl+Shift+F1, or
`palang complain …` in Palang's terminal), oldest first. Claude reads
this at the start of each session.

## 2026-10-23 03:01

When running palang from terminal. changes to panel sizing isn't remembed. But from the menu launch of Palang it works.

→ Handled in 8523a64: `palang .` (any start with files or a folder) now restores the panel; Ctrl+C, a closed terminal or `kill` now quit the normal way, saving the session (and asking about unsaved changes).

## 2026-10-23 03:01

Palang desktop icon is just generic image logo, not the palang logo.

→ Handled in 5511500: the icon file's `<svg` tag sat past byte 256, so the MATE menu (GTK3) couldn't recognise it as an image. Install the new `.deb` to see it.

## 2026-10-23 03:15 · neuralnetwork.js:8

when you type charachter "(" or "{" no closer is added next to the cursor. if type "(" i like ")" be auto added to the right of the text cursor.

→ Handled in 684c4da: ( [ { " ' close themselves (step over the closer, Backspace removes an empty pair, wraps a selection). Preferences → Typing, or `palang set auto-close-brackets false`.

## 2026-10-23 03:17 · neuralnetwork.js:8

After making open bracket and closing bracket "{}" the going in middle of it and pressing enter, it does not automatically move the closing bracket an extra line below, and the cursor does not automatically get indented in the line below. i'd like this behavior.

→ Handled in 4a1283f: Enter between {}, [] or () puts the closer on its own line and the cursor on an indented line in between.

## 2026-10-23 03:19 · neuralnetwork.js:13

I need specifc command to toggle cursor from terminal to editor and vice versa.

→ Handled in 0f2c604: Ctrl+` (or Ctrl+§ on a Swedish keyboard) moves the focus between the terminal and the editor.

## 2026-10-23 03:32 · neuralnetwork.js:9

would be nice with a markdown entire line command

→ Handled in bafb77c (sahi: meant "select"): Ctrl+L selects the whole line; again adds the next line.

## 2026-10-23 03:43 · neuralnetwork.js:13

the sidebar maybe could use a bit of styling, it looks so basic and bland. Maybe in lower part add somewhat opaque version of palang logo.

→ Handled in e8c862f: faint amber leopard at the bottom, folder name in bold amber, amber line under the header, warm glow (sahi picked mockup C).
