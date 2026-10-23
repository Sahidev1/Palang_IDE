/* palang-complaint-dialog.h — the complaint box (Ctrl+Shift+F1).
 *
 * A small dialog with a text box: write what bothers you, press Send (or
 * Ctrl+Enter), and it is added to the complaints file with the time and
 * what you were looking at (palang-complaints.h). If saving fails, the
 * dialog stays open with the text and shows why, so nothing is lost.
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define PALANG_TYPE_COMPLAINT_DIALOG (palang_complaint_dialog_get_type ())

G_DECLARE_FINAL_TYPE (PalangComplaintDialog, palang_complaint_dialog,
                      PALANG, COMPLAINT_DIALOG, AdwDialog)

/* @file: where complaints go (palang_complaints_get_file ()).
 * @context: (nullable): what the complaint is about, e.g.
 * "src/main.c:42"; shown in the dialog and written into the entry. */
PalangComplaintDialog *palang_complaint_dialog_new (GFile      *file,
                                                    const char *context);

/* Signal "sent": the complaint was saved. Emitted just before the dialog
 * closes. */

G_END_DECLS
