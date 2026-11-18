# How the complaint box works

A guide to the complaint box (2026-10-23): sahi writes down what bothers
them while using Palang, and Claude reads it at the start of each session.
It is a small module, `src/feedback/`, plus a little wiring in `app/`.
The end of this guide covers the test fixes made right after it, because
they changed how Palang's tests wait.

## The big picture

```
Ctrl+Shift+F1 (win.complain)          palang complain TEXT (in the terminal)
        │                                        │
palang-window-feedback.c                 palang-command-line.c
  context: "src/main.c:42"                 context: "terminal"
  PalangComplaintDialog ──Send──┐                │
                                ▼                ▼
                   palang-complaints.c: format_entry () + append ()
                                │
                                ▼
            the file of the `complaints-file` setting
            (sahi: COMPLAINTS.md in the repo; empty: ~/.local/share/palang/complaints.md)
                                │
                                ▼
          Claude, at the start of a session (CLAUDE.md, "Next step" 0)
```

`feedback/` uses no other module of ours (ARCHITECTURE.md §2): the window
works out the context and the file, and hands both to the dialog.

## The file

`palang-complaints.c` has three small functions:

- **`palang_complaints_get_file (setting)`** turns the setting into a
  `GFile`. Empty means Palang's data folder: an installed Palang can't
  know where the repo is, so sahi points the setting at it once with
  `palang set complaints-file …`. `~/…` and relative paths start at the
  home folder (Palang's own working folder would be meaningless).
- **`palang_complaints_format_entry (time, context, text)`** makes one
  Markdown entry:

  ```
  ## 2026-10-23 14:05 · src/main.c:42

  The tab closes when I middle-click.
  ```

  It starts with a blank line, so the heading stays on its own line even
  if the file was edited by hand. It takes the time as a parameter instead
  of reading the clock, so the test can check the exact text.
- **`palang_complaints_append (file, entry)`** first tries to *create* the
  file. If that fails because it exists, it opens it for appending instead.
  That order tells it whether the file is new and needs its heading
  ("# Complaints about Palang …"), with no separate "does it exist?"
  check.

The write is synchronous (it waits until done). ARCHITECTURE.md says the
UI thread never blocks, but a few hundred bytes appended to a local file
take no noticeable time, and `config.ini` is saved the same way. Keeping
it synchronous keeps the code short, and `palang complain` (whose answer
must be ready when the function returns) can use the same function.

## The dialog

`PalangComplaintDialog` is an `AdwDialog` with a template
(`data/ui/palang-complaint-dialog.ui`): an "About src/main.c:42" line, a
`GtkTextView`, an error line (hidden), "Goes to ~/…" and a Send button.

Two GTK patterns appear here for the first time in Palang:

- **A widget action controls a button.** The dialog installs an action,
  `complaint.send` (`gtk_widget_class_install_action`), and the button
  refers to it by name (`action-name` in the .ui). Turning the action off
  with `gtk_widget_action_set_enabled ()` greys out the button. The
  buffer's `changed` signal does that whenever the text is only blank
  space, so the function that sends never sees an empty complaint.
- **Catching a key before the focused widget gets it.** Ctrl+Enter should
  send, but the text view handles Return itself (as a new line) before a
  shortcut of the dialog runs: GTK delivers keys to the focused widget
  first, then lets them *bubble* up to its parents. A key controller can
  instead run in the **capture** phase, on the way *down* from the window
  to the focused widget. The dialog adds one
  (`gtk_event_controller_set_propagation_phase (…, GTK_PHASE_CAPTURE)`)
  that sends on Ctrl+Enter and returns TRUE ("handled"), so the text view
  never sees it (CLAUDE.md gotcha 21). The language-server completion
  uses the same idea (gotcha 16).

Sending formats and appends the entry. If that fails, the dialog stays
open with the text and shows why: a complaint is the user's work, and
"never lose the user's work" (ARCHITECTURE.md §3) applies to it too. If
it succeeds, the dialog emits `sent` and closes, and the window shows a
"Complaint saved" toast.

## The wiring in the window

`palang-window-feedback.c` is the window's part (like `-palette.c` and
`-lsp.c`):

- **`win.complain`** opens the dialog unless one is already showing.
- **The context** is the active tab's file, relative to the project
  folder if it's inside it (else `~/…`, or "Untitled"), and the cursor's
  line, counted from 1 for people (GTK counts from 0).
- The command table in `palang-application.c` gives it Ctrl+Shift+F1 and
  puts "Complain about Palang…" in the command palette.
  `commands_kept_in_terminal[]` lets the shortcut work while the terminal
  has the focus (gotcha 11).

`palang complain TEXT…` is one more row in the subcommand table of
`palang-command-line.c`. It joins the words like `palang set` does, so
no quotes are needed.

## Tests

`tests/test-complaints.c` (a UI test, because of the dialog):

- `get-file`, `format-entry`: the pure functions.
- `append`: the first entry creates the folder and the file with its
  heading; the second goes below without a second heading.
- `dialog-sends`: Send does nothing while the text is blank, then writes
  the entry with its context and emits `sent`.
- `dialog-keeps-text-on-error`: with a *file* where the folder should
  be, saving fails, `sent` isn't emitted and the text is still there.

`tests/test-command-line.c` checks `palang complain` (words joined,
"terminal" as the context, usage without text). Ctrl+Enter with real keys
was checked in the running app (MANUAL_TESTS.md, "Complaint box").

## Afterwards: tests on a busy computer

Right after this, `tools/build-deb.sh` failed in `test-window`. The
`.deb` build runs all test programs at once, so the CPU is busy, and some
tests only worked with time to spare. To reproduce it, run a test while
seven endless loops (`sh -c 'while :; do :; done'`) keep the CPU busy.

What was learned:

- **A text view on a busy computer learns about one line height per
  frame.** GTK measures lines in the background when it has time.
  Without that time, the view knows only the lines it has drawn, so
  scrolling to line 200 of a new tab moves only a bit further each
  frame. `palang_editor_go_to ()` used to give up after 60 frames. It now
  keeps going while the document's known height (the scroll
  adjustment's `upper`) still grows, and stops after a second without
  growth. That fixed a real, if rare, bug: "open at line" (search
  results, Problems, go to definition) stopping short.
- **Tests wait for a state, never for a fixed time.** Quick Open's test
  pressed Enter 200 ms after typing, sometimes before the list had
  re-sorted; it now waits until the wanted file is first.
  `answer_dialog ()` still waits half a second for a dialog to finish
  appearing (there's no signal for that), but now also at least 10
  frames, counted with a tick callback on the dialog.
- **Time limits are generous**, because they are only reached when
  something is really stuck: `wait_until ()` gives up after 30 s, and each
  UI test program gets 2 minutes (`ui_timeout` in `tests/meson.build`).

## Where to look

1. `src/feedback/palang-complaints.h`, then `palang-complaints.c`.
2. `src/feedback/palang-complaint-dialog.c` and its `.ui` file.
3. `src/app/palang-window-feedback.c`.
4. For the test timing: `keep_scrolling_to_cursor ()` in
   `src/editor/palang-editor.c`, and `answer_dialog ()` in
   `tests/test-window.c`.
